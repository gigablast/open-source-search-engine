#include "gb-include.h"

#include "Parms.h"
#include "File.h"
#include "Conf.h"
//#include "CollectionRec.h"
#include "TcpSocket.h"
#include "HttpRequest.h"
#include "Pages.h"         // g_pages
#include "Tagdb.h"        // g_tagdb
#include "Catdb.h"
#include "Collectiondb.h"
#include "HttpMime.h"      // atotime()
//#include "Msg28.h"
//#include "Sync.h"
#include "Indexdb.h" // for MIN_TRUNC
#include "SearchInput.h"
#include "Unicode.h"
#include "Threads.h"
#include "Spider.h" // MAX_SPIDER_PRIORITIES
#include "Statsdb.h"
#include "Sections.h"
#include "Msg17.h"
#include "Process.h"
#include "Repair.h"
#include "Ads.h"
#include "LanguagePages.h"
#include "PingServer.h"
#include "Users.h"
#include "Proxy.h"
#include "hash.h"
#include "Test.h"
#include "Rebalance.h"

// width of input box in characters for url filter expression
#define REGEX_TXT_MAX 80

Parms g_parms;


//#include "Tfndb.h"
#include "Spider.h"
#include "Tagdb.h"
#include "Indexdb.h"
#include "Datedb.h"
//#include "Checksumdb.h"
#include "Clusterdb.h"
#include "Collectiondb.h"

//
// new functions to extricate info from parm recs
//


long getDataSizeFromParmRec ( char *rec ) {
	return *(long *)(rec+sizeof(key96_t));
}

char *getDataFromParmRec ( char *rec ) {
	return rec+sizeof(key96_t)+4;
}

collnum_t getCollnumFromParmRec ( char *rec ) {
	key96_t *k = (key96_t *)rec;
	return (collnum_t)k->n1;
}

// for parms that are arrays...
short getOccNumFromParmRec ( char *rec ) {
	key96_t *k = (key96_t *)rec;
	return (short)((k->n0>>16));
}

Parm *getParmFromParmRec ( char *rec ) {
	key96_t *k = (key96_t *)rec;
	long cgiHash32 = (k->n0 >> 32);
	return g_parms.getParmFast2 ( cgiHash32 );
}

long getHashFromParmRec ( char *rec ) {
	key96_t *k = (key96_t *)rec;
	long cgiHash32 = (k->n0 >> 32);
	return cgiHash32;
}

// . occNum is index # for parms that are arrays. it is -1 if not used.
// . collnum is -1 for g_conf, which is not a collrec
// . occNUm is -1 for a non-array parm
key96_t makeParmKey ( collnum_t collnum , Parm *m , short occNum ) {
	key96_t k;
	k.n1 = collnum;
	k.n0 = (unsigned long)m->m_cgiHash; // 32 bit
	k.n0 <<= 16;
	k.n0 |= (unsigned short)occNum;
	// blanks
	k.n0 <<= 16;
	// delbit. 1 means positive key
	k.n0 |= 0x01;
	// test
	if ( getCollnumFromParmRec ((char *)&k)!=collnum){char*xx=NULL;*xx=0;}
	if ( getOccNumFromParmRec ((char *)&k)!=occNum){char*xx=NULL;*xx=0;}
	return k;
}

//////////////////////////////////////////////
//
// Command Functions. All return false if block... yadayada
//
//////////////////////////////////////////////

////////
//
// . do commands this way now
// . when handleRequest4 receives a special "command" parmdb rec
//   it calls executes the cmd, one of the functions listed below
// . all these Command*() functions are called in updateParm() below
// . they return false if they would block and they'll call your callback
//   specified in you "we" the WaitEntry
// . they return true with g_errno set on error, set to 0 on success
//
////////

// . require user manually execute this to prevent us fucking up the data
//   at first initially because of a bad hosts.conf file!!!
// . maybe put a red 'A' in the hosts table on the web page to indicate
//   we detected records that don't belong to our shard so user knows to
//   rebalance?
// . we'll show it in a special msg box on all admin pages if required
bool CommandRebalance ( char *rec ) {
	g_rebalance.m_userApproved = true;
	return true;
}

bool CommandInsertUrlFiltersRow ( char *rec ) {
	// caller must specify collnum
	collnum_t collnum = getCollnumFromParmRec ( rec );
	if ( collnum < 0 ) {
		log("parms: bad collnum for insert row");
		g_errno = ENOCOLLREC;
		return true;
	}
	// sanity
	long dataSize = getDataSizeFromParmRec ( rec );
	if ( dataSize <= 1 ) {
		log("parms: insert row data size = %li bad!",dataSize);
		g_errno = EBADENGINEER;
		return true;
	}
	// need this
	CollectionRec *cr = g_collectiondb.getRec ( collnum );
	// get the row #
	char *data = getDataFromParmRec ( rec );
	long rowNum = atol(data);//*(long *)data;
	// scan all parms for url filter parms
	for ( long i = 0 ; i < g_parms.m_numParms ; i++ ) {
		Parm *m = &g_parms.m_parms[i];
		// parm must be a url filters parm
		if ( m->m_page != PAGE_FILTERS ) continue;
		// must be an array!
		if ( ! m->isArray() ) continue;
		// sanity check
		if ( m->m_obj != OBJ_COLL ) { char *xx=NULL;*xx=0; }
		// . add that row
		// . returns false and sets g_errno on error
		if ( ! g_parms.insertParm ( i, rowNum,(char *)cr)) return true;
	}
	return true;
}

bool CommandRemoveConnectIpRow ( char *rec ) {
	// caller must specify collnum
	//collnum_t collnum = getCollnumFromParmRec ( rec );
	//if ( collnum < 0 ) {
	//	g_errno = ENOCOLLREC;
	//	log("parms: bad collnum for remove row");
	//	return true;
	//}
	// sanity
	long dataSize = getDataSizeFromParmRec ( rec );
	if ( dataSize <= 1 ) {
		log("parms: insert row data size = %li bad!",dataSize);
		g_errno = EBADENGINEER;
		return true;
	}
	// need this
	//CollectionRec *cr = g_collectiondb.getRec ( collnum );
	// get the row #
	char *data = getDataFromParmRec ( rec );
	long rowNum = atol(data);
	// scan all parms for url filter parms
	for ( long i = 0 ; i < g_parms.m_numParms ; i++ ) {
		Parm *m = &g_parms.m_parms[i];
		// parm must be a url filters parm
		if ( m->m_page != PAGE_SECURITY ) continue;
		// must be an array!
		if ( ! m->isArray() ) continue;
		// sanity check
		if ( m->m_obj != OBJ_CONF ) { char *xx=NULL;*xx=0; }
		// . nuke that parm's element
		// . returns false and sets g_errno on error
		if (!g_parms.removeParm(i,rowNum,(char *)&g_conf))return true;
	}
	return true;
}

bool CommandRemoveUrlFiltersRow ( char *rec ) {
	// caller must specify collnum
	collnum_t collnum = getCollnumFromParmRec ( rec );
	if ( collnum < 0 ) {
		g_errno = ENOCOLLREC;
		log("parms: bad collnum for remove row");
		return true;
	}
	// sanity
	long dataSize = getDataSizeFromParmRec ( rec );
	if ( dataSize <= 1 ) {
		log("parms: insert row data size = %li bad!",dataSize);
		g_errno = EBADENGINEER;
		return true;
	}
	// need this
	CollectionRec *cr = g_collectiondb.getRec ( collnum );
	// get the row #
	char *data = getDataFromParmRec ( rec );
	long rowNum = atol(data);
	// scan all parms for url filter parms
	for ( long i = 0 ; i < g_parms.m_numParms ; i++ ) {
		Parm *m = &g_parms.m_parms[i];
		// parm must be a url filters parm
		if ( m->m_page != PAGE_FILTERS ) continue;
		// must be an array!
		if ( ! m->isArray() ) continue;
		// sanity check
		if ( m->m_obj != OBJ_COLL ) { char *xx=NULL;*xx=0; }
		// . nuke that parm's element
		// . returns false and sets g_errno on error
		if ( ! g_parms.removeParm ( i,rowNum,(char *)cr)) return true;
	}
	return true;
}

// customCrawl:
// 0 for regular collection
// 1 for custom crawl
// 2 for bulk job
// . returns false if blocks true otherwise
bool CommandAddColl ( char *rec , char customCrawl ) {

	// caller must specify collnum
	collnum_t newCollnum = getCollnumFromParmRec ( rec );

	// sanity.
	if ( newCollnum < 0 ) {
		g_errno = ENOCOLLREC;
		log("parms: bad collnum for AddColl");
		return true;
	}

	char *data = rec + sizeof(key96_t) + 4;
	long dataSize = *(long *)(rec + sizeof(key96_t));
	// collection name must be at least 2 bytes (includes \0)
	if ( dataSize <= 1 ) { char *xx=NULL;*xx=0; }

	// then collname, \0 terminated
	char *collName = data;

	if ( gbstrlen(collName) > MAX_COLL_LEN ) {
		log("crawlbot: collection name too long");
		return true;
	}

	// this saves it to disk! returns false and sets g_errno on error.
	if ( ! g_collectiondb.addNewColl ( collName,
					   customCrawl ,
					   NULL ,  // copy from
					   0  , // copy from len
					   true , // save?
					   newCollnum
					   ) )
		// error! g_errno should be set
		return true;

	return true;
}

// all nodes are guaranteed to add the same collnum for the given name
bool CommandAddColl0 ( char *rec ) { // regular collection
	return CommandAddColl ( rec , 0 );
}

bool CommandAddColl1 ( char *rec ) { // custom crawl
	return CommandAddColl ( rec , 1 );
}

bool CommandAddColl2 ( char *rec ) { // bulk job
	return CommandAddColl ( rec , 2 );
}

// . returns true and sets g_errno on error
// . returns false if would block
bool CommandDeleteColl ( char *rec , WaitEntry *we ) {
	collnum_t collnum = getCollnumFromParmRec ( rec );
	// the delete might block because the tree is saving and we can't
	// remove our collnum recs from it while it is doing that
	if ( ! g_collectiondb.deleteRec2 ( collnum ) )
		// we blocked, we->m_callback will be called when done
		return false;
	// delete is successful
	return true;
}

// . returns true and sets g_errno on error
// . returns false if would block
bool CommandDeleteColl2 ( char *rec , WaitEntry *we ) {
	char *data = rec + sizeof(key96_t) + 4;
	char *coll = (char *)data;
	collnum_t collnum = g_collectiondb.getCollnum ( coll );
	if ( collnum < 0 ) {
		g_errno = ENOCOLLREC;
		return true;;
	}
	// the delete might block because the tree is saving and we can't
	// remove our collnum recs from it while it is doing that
	if ( ! g_collectiondb.deleteRec2 ( collnum ) )
		// we blocked, we->m_callback will be called when done
		return false;
	// delete is successful
	return true;
}

// . returns true and sets g_errno on error
// . returns false if would block
bool CommandRestartColl ( char *rec , WaitEntry *we ) {

	collnum_t newCollnum = getCollnumFromParmRec ( rec );

	// . data is the collnum in ascii.
	// . from "&restart=467" for example
	char *data = rec + sizeof(key96_t) + 4;
	long dataSize = *(long *)(rec + sizeof(key96_t));
	if ( dataSize < 1 ) { char *xx=NULL;*xx=0; }
	collnum_t oldCollnum = atol(data);

	if ( oldCollnum < 0 || 
	     oldCollnum >= g_collectiondb.m_numRecs ||
	     ! g_collectiondb.m_recs[oldCollnum] ) {
		log("parms: invalid collnum %li to restart",(long)oldCollnum);
		return true;
	}

	// this can block if tree is saving, it has to wait
	// for tree save to complete before removing old
	// collnum recs from tree
	if ( ! g_collectiondb.resetColl2 ( oldCollnum ,
					   newCollnum ,
					   false ) ) // purgeSeeds?
		// we blocked, we->m_callback will be called when done
		return false;

	// turn on spiders on new collrec. collname is same but collnum
	// will be different.
	CollectionRec *cr = g_collectiondb.getRec ( newCollnum );
	// if reset from crawlbot api page then enable spiders
	// to avoid user confusion
	if ( cr ) cr->m_spideringEnabled = 1;

	// all done
	return true;
}

// . returns true and sets g_errno on error
// . returns false if would block
bool CommandResetColl ( char *rec , WaitEntry *we ) {

	collnum_t newCollnum = getCollnumFromParmRec ( rec );

	// . data is the collnum in ascii.
	// . from "&restart=467" for example
	char *data = rec + sizeof(key96_t) + 4;
	long dataSize = *(long *)(rec + sizeof(key96_t));
	if ( dataSize < 1 ) { char *xx=NULL;*xx=0; }
	collnum_t oldCollnum = atol(data);

	if ( oldCollnum < 0 || 
	     oldCollnum >= g_collectiondb.m_numRecs ||
	     ! g_collectiondb.m_recs[oldCollnum] ) {
		log("parms: invalid collnum %li to reset",(long)oldCollnum);
		return true;
	}

	// this will not go through if tree is saving, it has to wait
	// for tree save to complete before removing old
	// collnum recs from tree. so return false in that case so caller
	// will know to re-call later.
	if ( ! g_collectiondb.resetColl2 ( oldCollnum ,
					   newCollnum ,
					   true ) ) // purgeSeeds?
		// we blocked, we->m_callback will be called when done
		return false;

	// turn on spiders on new collrec. collname is same but collnum
	// will be different.
	CollectionRec *cr = g_collectiondb.getRec ( newCollnum );
	// if reset from crawlbot api page then enable spiders
	// to avoid user confusion
	if ( cr ) cr->m_spideringEnabled = 1;

	return true;
}

bool CommandParserTestInit ( char *rec ) {
	// enable testing for all other hosts
	g_conf.m_testParserEnabled = 1;
	// reset all files
	g_test.removeFiles();
	// turn spiders on globally
	g_conf.m_spideringEnabled = 1;
	//g_conf.m_webSpideringEnabled = 1;
	// turn on for test coll too
	CollectionRec *cr = g_collectiondb.getRec("test");
	// turn on spiders
	if ( cr ) cr->m_spideringEnabled = 1;
	// if we are not host 0, turn on spiders for testing
	if ( g_hostdb.m_myHost->m_hostId != 0 ) return true;
	// start the test loop to inject urls for parsing/spidering
	g_test.initTestRun();
	// done 
	return true;
}

bool CommandSpiderTestInit ( char *rec ) {
	// enable testing for all other hosts
	g_conf.m_testSpiderEnabled = 1;
	// reset all files
	g_test.removeFiles();
	// turn spiders on globally
	g_conf.m_spideringEnabled = 1;
	//g_conf.m_webSpideringEnabled = 1;
	// turn on for test coll too
	CollectionRec *cr = g_collectiondb.getRec("test");
	// turn on spiders
	if ( cr ) cr->m_spideringEnabled = 1;
	// if we are not host 0, turn on spiders for testing
	if ( g_hostdb.m_myHost->m_hostId != 0 ) return true;
	// start the test loop to inject urls for parsing/spidering
	g_test.initTestRun();
	// done 
	return true;
}

bool CommandSpiderTestCont ( char *rec ) {
	// enable testing for all other hosts
	g_conf.m_testSpiderEnabled = 1;
	// turn spiders on globally
	g_conf.m_spideringEnabled = 1;
	//g_conf.m_webSpideringEnabled = 1;
	// turn on for test coll too
	CollectionRec *cr = g_collectiondb.getRec("test");
	// turn on spiders
	if ( cr ) cr->m_spideringEnabled = 1;
	// done 
	return true;
}

// some of these can block a little. if threads are off, a lot!
bool CommandMerge ( char *rec ) {
	// most of these are probably already in good shape
	//g_checksumdb.getRdb()->attemptMerge (1,true);
	g_clusterdb.getRdb()->attemptMerge  (1,true); // niceness, force?
	g_tagdb.getRdb()->attemptMerge     (1,true);
	g_catdb.getRdb()->attemptMerge      (1,true);
	//g_tfndb.getRdb()->attemptMerge      (1,true);
	g_spiderdb.getRdb()->attemptMerge   (1,true);
	// these 2 will probably need the merge the most
	g_indexdb.getRdb()->attemptMerge    (1,true);
	g_datedb.getRdb()->attemptMerge     (1,true);
	g_titledb.getRdb()->attemptMerge    (1,true);
	//g_sectiondb.getRdb()->attemptMerge  (1,true);
	g_statsdb.getRdb()->attemptMerge    (1,true);
	g_linkdb .getRdb()->attemptMerge    (1,true);
	return true;
}


bool CommandMergePosdb ( char *rec ) {
	g_posdb.getRdb()->attemptMerge    (1,true);
	return true;
}


bool CommandMergeSectiondb ( char *rec ) {
	g_sectiondb.getRdb()->attemptMerge    (1,true); // nice , force
	return true;
}


bool CommandMergeTitledb ( char *rec ) {
	g_titledb.getRdb()->attemptMerge    (1,true);
	return true;
}


bool CommandMergeSpiderdb ( char *rec ) {
	g_spiderdb.getRdb()->attemptMerge    (1,true);
	return true;
}


bool CommandDiskPageCacheOff ( char *rec ) {
	g_process.resetPageCaches();
	return true;
}

bool CommandDiskDump ( char *rec ) {
	//g_checksumdb.getRdb()->dumpTree ( 1 ); // niceness
	g_clusterdb.getRdb()->dumpTree  ( 1 );
	g_tagdb.getRdb()->dumpTree     ( 1 );  
	g_catdb.getRdb()->dumpTree      ( 1 );
	//g_tfndb.getRdb()->dumpTree      ( 1 );   
	g_spiderdb.getRdb()->dumpTree   ( 1 );
	g_posdb.getRdb()->dumpTree    ( 1 );
	//g_datedb.getRdb()->dumpTree     ( 1 );
	g_titledb.getRdb()->dumpTree    ( 1 );
	//g_sectiondb.getRdb()->dumpTree  ( 1 );
	g_statsdb.getRdb()->dumpTree    ( 1 );
	g_linkdb.getRdb() ->dumpTree    ( 1 );
	g_errno = 0;
	return true;
}


bool CommandJustSave ( char *rec ) {
	// returns false if blocked, true otherwise
	g_process.save ();
	// always return true here
	return true;
}

bool CommandSaveAndExit ( char *rec ) {
	// return true if this blocks
	g_process.shutdown ( false , NULL , NULL );
	return true;
}

bool CommandUrgentSaveAndExit ( char *rec ) {
	// "true" means urgent
	g_process.shutdown ( true );
	return true;
}

bool CommandReloadLanguagePages ( char *rec ) {
	g_languagePages.reloadPages();
	return true;
}

bool CommandClearKernelError ( char *rec ) {
	g_hostdb.m_myHost->m_kernelErrors = 0;
	return true;
}

bool CommandPowerNotice ( long hasPower ) {

	//long hasPower = r->getLong("haspower",-1);
	log("powermo: received haspower=%li",hasPower);
	if ( hasPower != 0 && hasPower != 1 ) return true;

	// did power state change? if not just return true
	if (   g_process.m_powerIsOn &&   hasPower ) return true;
	if ( ! g_process.m_powerIsOn && ! hasPower ) return true;
	
	if ( hasPower ) {
		log("powermo: power is regained");
		g_process.m_powerIsOn = true;
		return true;
	}

	// if it was on and went off...
	// now it is off
	log("powermo: power was lost");
	// . SpiderLoop.cpp will not launch any more spiders as
	//   long as the power is off
	// . autosave should kick in every 30 seconds
	g_process.m_powerIsOn = false;
	// note the autosave
	log("powermo: disabling spiders, suspending merges, disabling "
	    "tree writes and saving.");
	// tell Process.cpp::save2() to save the blocking caches too!
	//g_process.m_pleaseSaveCaches = true;
	// . save everything now... this may block some when saving the
	//   caches... then do not do ANY writes... 
	// . RdbMerge suspends all merging if power is off
	// . Rdb.cpp does not allow any adds if power is off. it will
	//   send back an ETRYAGAIN...
	// . if a tree is being dumped, this will keep re-calling
	//   Process.cpp::save2()
	g_process.save();

	// also send an email if we are host #0
	if ( g_hostdb.m_myHost->m_hostId != 0 ) return true;
	if ( g_proxy.isProxy() ) return true;

	char tmp[128];
	Host *h0 = g_hostdb.getHost ( 0 );
	long ip0 = 0;
	if ( h0 ) ip0 = h0->m_ip;
	sprintf(tmp,"%s: POWER IS OFF",iptoa(ip0));

	g_pingServer.sendEmail ( NULL  , // Host ptr
				 tmp   , // msg
				 true  , // sendToAdmin
				 false , // oom?
				 false , // kernel error?
				 true  , // parm change?
				 // force it? even if disabled?
				 false  );
	return true;
}


bool CommandPowerOnNotice ( char *rec ) {
	return CommandPowerNotice ( 1 );
}

bool CommandPowerOffNotice ( char *rec ) {
	return CommandPowerNotice ( 0 );
}

bool CommandInSync ( char *rec ) {
	g_parms.m_inSyncWithHost0 = true;
	return true;
}

//////////////////////
//
// end new commands
//
//////////////////////


static bool printDropDown   ( long n , SafeBuf* sb, char *name, 
			      long selet , 
			      bool includeMinusOne ,
			      bool includeMinusTwo ) ;

extern bool closeAll ( void *state, void (* callback)(void *state) );
extern bool allExit ( ) ;
/*
class Checksum {
public:
	Checksum() : m_sum1( 0xffff ), m_sum2( 0xffff ) {}

	void addIn( const uint16_t *data, size_t size, FILE *f = 0 ) {
		// if an odd len of data, add first byte, then do rest below
		if ( size % 2 != 0 ) { 
			m_sum1 += (uint16_t)*(uint8_t *)data;
			m_sum2 += m_sum1;

			size--;
			data = (uint16_t *)(((uint8_t *)data)+1);
		}

		size_t len = size/2;
		while ( len ) {
			unsigned tlen = len;
			// . 360 is largest amnt of sums that can be performed
			//   without overflow
			if ( len > 360 ) tlen = 360;
			len -= tlen;
			do {
				m_sum1 += *data++;
				m_sum2 += m_sum1;
			} while ( --tlen );

			m_sum1 = (m_sum1 & 0xffff) + (m_sum1 >> 16);
			m_sum2 = (m_sum2 & 0xffff) + (m_sum2 >> 16);
		}
	}

	void addInStrings( const uint16_t *data, long cnt, long size ) {
		while ( cnt ) {
			const uint16_t *origData = data;
			long len = gbstrlen((char *)data);

			// if an odd len of data, add first byte, 
			// then do rest below
			if ( len % 2 != 0 ) { 
				m_sum1 += (uint16_t)*(uint8_t *)data;
				m_sum2 += m_sum1;

				len--;
				data = (uint16_t *)(((uint8_t *)data)+1);
			}

			len /= 2;
			while ( len ) {
				unsigned tlen = len;
				// . 360 = largest amnt of sums that can be 
				//   performed without overflow
				if ( len > 360 ) tlen = 360;
				len -= tlen;
				do {
					m_sum1 += *data++;
					m_sum2 += m_sum1;
				} while ( --tlen );

				m_sum1 = (m_sum1 & 0xffff) + (m_sum1 >> 16);
				m_sum2 = (m_sum2 & 0xffff) + (m_sum2 >> 16);
			}

			cnt--;
			data = (uint16_t *)((char *)origData + size);
		}
	}

	void finalize() {
		m_sum1 = (m_sum1 & 0xffff) + (m_sum1 >> 16);
		m_sum2 = (m_sum2 & 0xffff) + (m_sum2 >> 16);
	}

	uint32_t getSum() const {
		return ( m_sum2 << 16 | m_sum1 );
	}

private:
	uint32_t m_sum1;
	uint32_t m_sum2;
};
*/

Parms::Parms ( ) {
	m_isDefaultLoaded = false;
	m_inSyncWithHost0 = false;
}

void Parms::detachSafeBufs ( CollectionRec *cr ) {
	for ( long i = 0 ; i < m_numParms ; i++ ) {
		Parm *m = &m_parms[i];
		if ( m->m_type != TYPE_SAFEBUF ) continue;
		if ( m->m_obj != OBJ_COLL ) continue;
		if ( m->m_off < 0 ) continue;
		long max = 1;
		// this will be zero if not an array.
		// othewise it is the # of elements in the array
		if ( m->m_size > max ) max = m->m_size;
		// an array of safebufs? m->m_size will be > 1 then.
		for ( long j = 0 ; j < max ; j++ ) {
			// get it
			SafeBuf *sb = (SafeBuf *)((char *)cr + m->m_off +
						  j*sizeof(SafeBuf));
			sb->detachBuf();
		}
	}
}
/*
unsigned long Parms::calcChecksum() {
	Checksum cs;

	for ( long i = 0 ; i < m_numParms ; i++ ) {
		Parm *m = &m_parms[i];
		if ( m->m_obj == OBJ_SI ) continue;
		if ( m->m_off < 0 ) continue;
		if ( m->m_type == TYPE_COMMENT ) continue;
		if ( m->m_type == TYPE_MONOD2  ) continue;
		if ( m->m_type == TYPE_MONOM2  ) continue;
		if ( m->m_type == TYPE_CMD     ) continue;
		if ( m->m_type == TYPE_LONG_CONST ) continue;

		long size = 0;
		if ( m->m_type == TYPE_CHECKBOX       ) size = 1;
		if ( m->m_type == TYPE_CHAR           ) size = 1;
		if ( m->m_type == TYPE_CHAR2          ) size = 1;
		if ( m->m_type == TYPE_BOOL           ) size = 1;
		if ( m->m_type == TYPE_BOOL2          ) size = 1;
		if ( m->m_type == TYPE_PRIORITY       ) size = 1;
		if ( m->m_type == TYPE_PRIORITY2      ) size = 1;
		//if ( m->m_type == TYPE_DIFFBOT_DROPDOWN) size = 1;
		if ( m->m_type == TYPE_PRIORITY_BOXES ) size = 1;
		if ( m->m_type == TYPE_RETRIES        ) size = 1;
		if ( m->m_type == TYPE_TIME           ) size = 6;
		if ( m->m_type == TYPE_DATE2          ) size = 4;
		if ( m->m_type == TYPE_DATE           ) size = 4;
		if ( m->m_type == TYPE_FLOAT          ) size = 4;
		if ( m->m_type == TYPE_IP             ) size = 4;
		if ( m->m_type == TYPE_RULESET        ) size = 4;
		if ( m->m_type == TYPE_LONG           ) size = 4;
		if ( m->m_type == TYPE_LONG_LONG      ) size = 8;
		if ( m->m_type == TYPE_STRING         ) size = m->m_size;
		if ( m->m_type == TYPE_STRINGBOX      ) size = m->m_size;
		if ( m->m_type == TYPE_STRINGNONEMPTY ) size = m->m_size;
		if ( m->m_type == TYPE_SAFEBUF        ) size = m->m_size;
		if ( m->m_type == TYPE_SITERULE       ) size = 4;


		// if we have an array
		long cnt = 1;
		if (m->m_fixed > 0) {
			size *= m->m_fixed;
			cnt = m->m_fixed;
		}
		else {
			size *= m->m_max;
			cnt = m->m_max;
		}

		uint16_t *p = NULL;
		if ( m->m_obj == OBJ_CONF ) {
			p = (uint16_t *)((char *)&g_conf + m->m_off);
			if (m->m_type == TYPE_STRING || 
			    m->m_type == TYPE_STRINGBOX || 
			    m->m_type == TYPE_STRINGNONEMPTY ) {
				cs.addInStrings( p, 
						 cnt,
						 m->m_size );
			}
			else if ( m->m_type == TYPE_SAFEBUF ) {
				uint16_t *p2;
				SafeBuf *sb2 = (SafeBuf *)p;
				p2 = (uint16_t *)sb2->getBufStart();
				cs.addIn( p2 , sb2->length() );
			}
			else {
				cs.addIn( p, size );
			}
		}
	       	else if ( m->m_obj == OBJ_COLL )  {
			collnum_t j = g_collectiondb.getFirstCollnum ();
			while ( j >= 0 ) {
				CollectionRec *cr = g_collectiondb.getRec( j );
				p = (uint16_t *)((char *)cr + m->m_off);
				if (m->m_type == TYPE_STRING || 
				    m->m_type == TYPE_STRINGBOX || 
				    m->m_type == TYPE_STRINGNONEMPTY ) {
					cs.addInStrings( p, 
							 cnt,
							 m->m_size );
				}
				else if ( m->m_type == TYPE_SAFEBUF ) {
					uint16_t *p2;
					SafeBuf *sb2 = (SafeBuf *)p;
					p2 = (uint16_t *)sb2->getBufStart();
					cs.addIn( p2 , sb2->length() );
				}
				else {
					cs.addIn( p, size );
				}
				j = g_collectiondb.getNextCollnum ( j );
			}
		} 
	}

	cs.finalize();

	return cs.getSum();
}
*/

// . returns false if blocked, true otherwise
// . sets g_errno on error
// . must ultimately send reply back on "s"
bool Parms::sendPageGeneric ( TcpSocket *s , HttpRequest *r , long page ,
			      char *cookie , SafeBuf *pageBuf ,
			      char *collOveride ,
			      bool isJSON ) {

	char  buf [ 128000 ];
	SafeBuf stackBuf(buf,128000);

	SafeBuf *sb = &stackBuf;

	if ( pageBuf ) sb = pageBuf;

	//char *p    = buf;
	//char *pend = buf + 128000;

	//long  user     = g_pages.getUserType              ( s , r );
	//char *username = g_users.getUsername(r);
	long  fromIp   = s->m_ip;

	//char *pwd  = r->getString ("pwd");

	char *coll = r->getString ("c");
	if ( ! coll || ! coll[0] )
		//coll = g_conf.m_defaultColl;
		coll = g_conf.getDefaultColl( r->getHost(), r->getHostLen() );

	if ( collOveride ) coll = collOveride;

	long nc = r->getLong("nc",1);
	long pd = r->getLong("pd",1);

	//p += sprintf(p, "<script type=\"text/javascript\">"
	if ( ! isJSON )
		sb->safePrintf (  
				"<script type=\"text/javascript\">"
				"function filterRow(str) {"
				//"alert ('string: ' + str);"
				"var tab = document.all ? document.all"
				"['parmtable'] :"
				"               document.getElementById ?"
				"document.getElementById('parmtable') : null;"
				"  for(var j = 1; j < tab.rows.length;j++) {" 
				" if(tab.rows[j].innerHTML.indexOf(str) < 0) {"
				"      tab.rows[j].style.display = 'none';"
				"    } else {"
				"      tab.rows[j].style.display = '';"
				"    }"
				"  }"
				"}\n"
				"function checkAll(form, name, num) {\n "
				"    for (var i = 0; i < num; i++) {\n"
				"      var nombre;\n"
				"      if( i > 0) nombre = name + i;\n"
				"      else nombre = name;\n"
				"   var e = document.getElementById(nombre);\n"
				"      e.checked = !e.checked;\n"
				//"      if ( e.value == 'Y' ) e.value='N';"
				//"    else if ( e.value == 'N' ) e.value='Y';"
				"    }\n"
				"}\n"
				"</script>");
	
	// print standard header 
	if ( ! pageBuf && ! isJSON )
		g_pages.printAdminTop ( sb , s , r );

	

	// print the start of the table
	char *tt = "None";
	if ( page == PAGE_LOG        ) tt = "Log Controls";
	if ( page == PAGE_MASTER     ) tt = "Master Controls";
	if ( page == PAGE_SECURITY   ) tt = "Security Controls";
	if ( page == PAGE_BASIC_PASSWORDS ) tt = "Security Controls";
	if ( page == PAGE_SPIDER     ) tt = "Spider Controls";
	if ( page == PAGE_SEARCH     ) tt = "Search Controls";
	if ( page == PAGE_ACCESS     ) tt = "Access Controls";
	if ( page == PAGE_FILTERS    ) tt = "Spider Scheduler";
	//if ( page == PAGE_PRIORITIES ) tt = "Priority Controls";
	//if ( page == PAGE_RULES      ) tt = "Site Rules";
	//if ( page == PAGE_SYNC       ) tt = "Sync";
	if ( page == PAGE_REPAIR     ) tt = "Repair Controls";
	//if ( page == PAGE_ADFEED     ) tt = "Ad Feed Controls";
	
	// special messages for spider controls
	char *e1 = "";
	char *e2 = "";
	if ( page == PAGE_SPIDER && ! g_conf.m_spideringEnabled )
		e1 = "<tr><td colspan=20><font color=#ff0000><b><center>"
			"Spidering is temporarily disabled in Master Controls."
			"</font></td></tr>\n";
	if ( page == PAGE_SPIDER && ! g_conf.m_addUrlEnabled )
		e2 = "<tr><td colspan=20><font color=#ff0000><b><center>"
			"Add url is temporarily disabled in Master Controls."
			"</font></td></tr>\n";

	// . page repair (PageRepair.cpp) has a status table BEFORE the parms
	//   iff we are doing a repair
	// . only one page for all collections, we have a parm that is
	//   a comma-separated list of the collections to repair. leave blank
	//   to repair all collections.
	if ( page == PAGE_REPAIR )
		g_repair.printRepairStatus ( sb , fromIp );
	
	char *THIS ;
	// when being called from Diffbot.cpp crawlbot page it is kind of
	// hacky and we want to print the url filters for the supplied 
	// collection dictated by collOveride. if we don't have this
	// fix here it ends up printing the url filters for the default
	// "main" collection
	if ( collOveride )
		THIS = (char *)g_collectiondb.getRec(collOveride);
	else
		THIS = g_parms.getTHIS ( r , page );

	if ( ! THIS ) {
		log("admin: Could not get parameter object.");
		return g_httpServer.sendErrorReply ( s , 505 , "Bad Request");
	}

	//CollectionRec *cr = (CollectionRec *)THIS;

	char bb [ 256];//MAX_COLL_LEN + 60 ];
	bb[0]='\0';
	//if ( user == USER_MASTER && page >= PAGE_OVERVIEW && c && c[0] ) 
	//	sprintf ( bb , " (%s)", c);
	//if ( page == PAGE_FILTERS )
	//	sprintf(bb,"(roundtime=%li roundnum=%li)"
	//		, cr->m_spiderRoundStartTime
	//		, cr->m_spiderRoundNum
	//		);

	// start the table
	if ( ! isJSON ) {
		sb->safePrintf( 
			       "\n"
			       "<table %s "
			       //"style=\"border-radius:15px;"
			       //"border:#6060f0 2px solid;"
			       //"\" "
			       //"width=100%% bgcolor=#%s "
			       //"bgcolor=black "
			       //"cellpadding=4 "
			       //"border=0 "//border=1 "
			       "id=\"parmtable\">"
			       "<tr><td colspan=20>"// bgcolor=#%s>"
			       ,TABLE_STYLE
			       //,DARKER_BLUE
			       //,DARK_BLUE
				);

		/*

		  take this out since we took out a ton of parms for
		  simplicties sake

		if ( page != PAGE_FILTERS )
			sb->safePrintf("<div style=\"float:left;\">" 
				       "filter:<input type=\"text\" "
				       "onkeyup=\"filterRow(this.value)\" "
				       "value=\"\"></div>"
				       );
		*/

		sb->safePrintf(//"<div style=\"margin-left:45%%;\">"
			       //"<font size=+1>"
			       "<center>"
			       "<b>%s</b>%s"
			       //"</font>"
			       "</center>"
			       //"</div>"
			       "</td></tr>%s%s\n",
			       tt,bb,e1,e2);
	}

	bool isCrawlbot = false;
	if ( collOveride ) isCrawlbot = true;
		
	// print the table(s) of controls
	//p= g_parms.printParms (p, pend, page, user, THIS, coll, pwd, nc, pd);
	g_parms.printParms ( sb, page, NULL , THIS, coll, NULL, nc, pd,
			     isCrawlbot , isJSON );

	// end the table
	if ( ! isJSON ) sb->safePrintf ( "</table>\n" );

	/*
	// if page is security
	if ( page == PAGE_SECURITY ){
		// a table of page names and description
		sb->safePrintf (
			  "<table align=center width=100%% border=1 "
			  "bgcolor=#d0d0e0 "
			  "cellpadding=2 border=0>" 
			  //"<tr><td colspan=2 bgcolor=#d0c0d0>"
			  "<tr><td colspan=2 bgcolor=#%s>"
			  "<center>"
			  "<b>Page Reference Table</b>"
			  //"</font>"
			  "</td></tr>\n"
			  "<tr><th>Page</th><th>Brief Description</th></tr>"
			  , DARK_BLUE);

		for ( long p=0; p < PAGE_INFO; p++ ){
			WebPage *page = g_pages.getPage(p);
			sb->safePrintf (
				  "<tr>"
				  "<td> <font size=-1>%s</font></td>"
				  "<td><font size=-1>%s</font></td></tr>\n",
				  page->m_filename , page->m_desc );
		}
		sb->safePrintf (  "</table>\n");
	}
	*/

	if ( ! isJSON ) sb->safePrintf ( "<br><br>\n" );

	// url filter page has a test table
	if ( page == PAGE_FILTERS && ! isJSON ) {

		// wrap up the form, print a submit button
		g_pages.printAdminBottom ( sb );

		/*
		CollectionRec *cr = (CollectionRec *)THIS;
		// if testUrl is provided, find in the table
		char testUrl [ 1025 ];
		char *tt = r->getString ( "test" , NULL );
		testUrl[0]='\0';
		if ( tt ) strncpy ( testUrl , tt , 1024 );
		char *tu = testUrl;
		if ( ! tu ) tu = "";
		char matchString[12];
		matchString[0] = '\0';
		if ( testUrl[0] ) {
			Url u;
			u.set ( testUrl , gbstrlen(testUrl) );
			//since we don't know the doc's quality, sfn, or
			//other stuff, just give default values
			long n = cr->getRegExpNum ( &u    , 
						    false ,  // links2gb?
						    false ,  // searchboxToGB
						    false ,  // onsite?
						    -1    ,  // docQuality
						    -1    ,  // hopCount
						    false ,  // siteInDmoz?
						    //-1  ,  // ruleset #
						    -1    ,  // langId
						    -1    ,  // parent priority
						    0     ,  // niceness
						    NULL  ,  // tagRec
						    false ,  // isRSS?
						    false ,  // isPermalink?
						    false ,  // new outlink?
						    -1    , // age
						    NULL  , // LinkInfo
						    NULL  , // parentUrl
						    -1    , // priority
						    false , // isAddUrl
						    false , // parentRSS?
						    false , // parentIsNew?
						    false , // parentIsPermlnk
						    false );// isIndexed?
			if ( n == -1 ) sprintf ( matchString , "default" );
			else           sprintf ( matchString, "%li", n+1 );
		}
		// test table
		sb.safePrintf (
			  //"</form><form method=get action=/cgi/14.cgi>"
			  //"<input type=hidden name="
			  "<table width=100%% cellpadding=4 border=1 "
			  "bgcolor=#%s>"
			  "<tr><td colspan=2 bgcolor=#%s><center>"
			  //"<font size=+1>"
			  "<b>"
			  "URL Filters Test</b>"
			  //"</font>"
			  "</td></tr>"
			  "<tr><td colspan=2>"
			  "<font size=1>"
			  "To test your URL filters simply enter a URL into "
			  "this box and submit it. The URL filter line number "
			  "that it matches will be displayed to the right."
			  "</font>"
			  "</td></tr>"
			  "<tr>"
			  "<td><b>Test URL</b></td>"
			  "<td><b>Matching Expression #</b></td>"
			  "</tr>"
			  "<tr>"
			  "<td><input type=text size=55 value=\"%s\" "
			  "name=test> "
			  "<input type=submit name=action value=test></td>"
			  "<td>%s</td></tr></table><br><br>\n" ,
			  LIGHT_BLUE , DARK_BLUE , testUrl , matchString );
		*/

		sb->safePrintf(
			       "<style>"
			       ".poo { background-color:#%s;}\n"
			       "</style>\n" ,
			       LIGHT_BLUE );

		sb->safePrintf (
			  "<table %s>"
			  "<tr><td colspan=2><center>"
			  "<b>"
			  "Supported URL Expressions</b>"
			  "</td></tr>"

			  "<tr class=poo><td>default</td>"
			  "<td>Matches every url."
			  "</td></tr>"

			  "<tr class=poo><td>^http://whatever</td>"
			  "<td>Matches if the url begins with "
			  "<i>http://whatever</i>"
			  "</td></tr>"

			  "<tr class=poo><td>$.css</td>"
			  "<td>Matches if the url ends with \".css\"."
			  "</td></tr>"

			  "<tr class=poo><td>foobar</td>"
			  "<td>Matches if the url CONTAINS <i>foobar</i>."
			  "</td></tr>"

			  "<tr class=poo><td>tld==uk,jp</td>"
			  "<td>Matches if url's TLD ends in \"uk\" or \"jp\"."
			  "</td></tr>"

			  /*
			  "<tr class=poo><td>doc:quality&lt;40</td>"
			  "<td>Matches if document quality is "
			  "less than 40. Can be used for assigning to spider "
			  "priority.</td></tr>"

			  "<tr class=poo><td>doc:quality&lt;40 && tag:ruleset==22</td>"
			  "<td>Matches if document quality less than 40 and "
			  "belongs to ruleset 22. Only for assinging to "
			  "spider priority.</td></tr>"

			  "<tr class=poo><td><nobr>"
			  "doc:quality&lt;40 && tag:manualban==1</nobr></td>"
			  "<td>Matches if document quality less than 40 and "
			  "is has a value of \"1\" for its \"manualban\" "
			  "tag.</td></tr>"

			  "<tr class=poo><td>tag:ruleset==33 && doc:quality&lt;40</td>"
			  "<td>Matches if document quality less than 40 and "
			  "belongs to ruleset 33. Only for assigning to "
			  "spider priority or a banned ruleset.</td></tr>"
			  */

			  "<tr class=poo><td>hopcount<4 && iswww</td>"
			  "<td>Matches if document has a hop count of 4, and "
			  "is a \"www\" url (or domain-only url).</td></tr>"
			  
			  "<tr class=poo><td>hopcount</td>"
			  "<td>All root urls, those that have only a single "
			  "slash for their path, and no cgi parms, have a "
			  "hop count of 0. Also, all RSS urls, ping "
			  "server urls and site roots (as defined in the "
			  "site rules table) have a hop count of 0. Their "
			  "outlinks have a hop count of 1, and the outlinks "
			  "of those outlinks a hop count of 2, etc."
			  "</td></tr>"

			  "<tr class=poo><td>sitepages</td>"
			  "<td>The number of pages that are currently indexed "
			  "for the subdomain of the URL. "
			  "Used for doing quotas."
			  "</td></tr>"

			  "<tr class=poo><td>domainpages</td>"
			  "<td>The number of pages that are currently indexed "
			  "for the domain of the URL. "
			  "Used for doing quotas."
			  "</td></tr>"

			  "<tr class=poo><td>siteadds</td>"
			  "<td>The number URLs manually added to the "
			  "subdomain of the URL. Used to guage a subdomain's "
			  "popularity."
			  "</td></tr>"

			  "<tr class=poo><td>domainadds</td>"
			  "<td>The number URLs manually added to the "
			  "domain of the URL. Used to guage a domain's "
			  "popularity."
			  "</td></tr>"



			  "<tr class=poo><td>isrss | !isrss</td>"
			  "<td>Matches if document is an rss feed. "
			  "When harvesting outlinks we <i>guess</i> if they "
			  "are an rss feed by seeing if their file extension "
			  "is xml, rss or rdf. Or if they are in an "
			  "alternative link tag.</td></tr>"

			  //"<tr class=poo><td>!isrss</td>"
			  //"<td>Matches if document is NOT an rss feed."
			  //"</td></tr>"

			  "<tr class=poo><td>ispermalink | !ispermalink</td>"
			  "<td>Matches if document is a permalink. "
			  "When harvesting outlinks we <i>guess</i> if they "
			  "are a permalink by looking at the structure "
			  "of the url.</td></tr>"

			  //"<tr class=poo><td>!ispermalink</td>"
			  //"<td>Matches if document is NOT a permalink."
			  //"</td></tr>"

			  /*
			  "<tr class=poo><td>outlink | !outlink</td>"
			  "<td>"
			  "<b>This is true if url being added to spiderdb "
			  "is an outlink from the page being spidered. "
			  "Otherwise, the url being added to spiderdb "
			  "directly represents the page being spidered. It "
			  "is often VERY useful to partition the Spiderdb "
			  "records based on this criteria."
			  "</td></tr>"
			  */

			  "<tr class=poo><td><nobr>isnewoutlink | !isnewoutlink"
			  "</nobr></td>"
			  "<td>"
			  "This is true since the outlink was not there "
			  "the last time we spidered the page we harvested "
			  "it from."
			  "</td></tr>"

			  "<tr class=poo><td>hasreply | !hasreply</td>"
			  "<td>"
			  "This is true if we have tried to spider "
			  "this url, even if we got an error while trying."
			  "</td></tr>"

			  "<tr class=poo><td>isnew | !isnew</td>"
			  "<td>"
			  "This is the opposite of hasreply above. A url "
			  "is new if it has no spider reply, including "
			  "error replies. So once a url has been attempted to "
			  "be spidered then this will be false even if there "
			  "was any kind of error."
			  "</td></tr>"

			  "<tr class=poo><td>lastspidertime >= "
			  "<b>{roundstart}</b></td>"
			  "<td>"
			  "This is true if the url's last spidered time "
			  "indicates it was spidered already for this "
			  "current round of spidering. When no more urls "
			  "are available for spidering, then gigablast "
			  "automatically sets {roundstart} to the current "
			  "time so all the urls can be spidered again. This "
			  "is how you do round-based spidering. "
			  "You have to use the respider frequency as well "
			  "to adjust how often you want things respidered."
			  "</td></tr>"
			  

			  //"<tr class=poo><td>!newoutlink</td>"
			  //"<td>Matches if document is NOT a new outlink."
			  //"</td></tr>"

			  "<tr class=poo><td>age</td>"
			  "<td>"
			  "How old is the doucment <b>in seconds</b>. "
			  "The age is based on the publication date of "
			  "the document, which could also be the "
			  "time that the document was last significantly "
			  "modified. If this date is unknown then the age "
			  "will be -1 and only match the expression "
			  "<i>age==-1</i>. "
			  "When harvesting links, we guess the publication "
			  "date of the oulink by detecting dates contained "
			  "in the url itself, which is popular among some "
			  "forms of permalinks. This allows us to put "
			  "older permalinks into a slower spider queue."
			  "</td></tr>"


			  "<tr class=poo><td>isaddurl | !isaddurl</td>"
			  "<td>"
			  "This is true if the url was added from the add "
			  "url interface. This replaces the add url priority "
			  "parm."
			  "</td></tr>"

			  "<tr class=poo><td>isinjected | !isinjected</td>"
			  "<td>"
			  "This is true if the url was directly "
			  "injected from the "
			  "/inject page or API."
			  "</td></tr>"

			  "<tr class=poo><td>isdocidbased | !isdocidbased</td>"
			  "<td>"
			  "This is true if the url was added from the "
			  "reindex interface. The request does not contain "
			  "a url, but only a docid, that way we can add "
			  "millions of search results very quickly without "
			  "having to lookup each of their urls. You should "
			  "definitely have this if you use the reindexing "
			  "feature. "
			  "You can set max spiders to 0 "
			  "for non "
			  "docidbased requests while you reindex or delete "
			  "the results of a query for extra speed."
			  "</td></tr>"

			  "<tr class=poo><td>ismanualadd | !ismanualadd</td>"
			  "<td>"
			  "This is true if the url was added manually. "
			  "Which means it matches isaddurl, isinjected, "
			  " or isdocidbased. as opposed to only "
			  "being discovered from the spider. "
			  "</td></tr>"

			  "<tr class=poo><td><nobr>inpingserver | !inpingserver"
			  "</nobr></td>"
			  "<td>"
			  "This is true if the url has an inlink from "
			  "a recognized ping server. Ping server urls are "
			  "hard-coded in Url.cpp. <b><font color=red> "
			  "pingserver urls are assigned a hop count of 0"
			  "</font></b>"
			  "</td></tr>"

			  "<tr class=poo><td>isparentrss | !isparentrss</td>"
			  "<td>"
			  "If a parent of the URL was an RSS page "
			  "then this will be matched."
			  "</td></tr>"

			  /*
			  "<tr class=poo><td>parentisnew | !parentisnew</td>"
			  "<td>"
			  "<b>Parent providing this outlink is not currently "
			  "in the index but is trying to be added right now. "
			  "</b>This is a special expression in that "
			  "it only applies to assigning spider priorities "
			  "to outlinks we are harvesting on a page.</b>" 
			  "</td></tr>"
			  */

			  "<tr class=poo><td>isindexed | !isindexed</td>"
			  "<td>"
			  "This url matches this if in the index already. "
			  "</td></tr>"

			  "<tr class=poo><td>errorcount==1</td>"
			  "<td>"
			  "The number of times the url has failed to "
			  "be indexed. 1 means just the last time, two means "
			  "the last two times. etc. Any kind of error parsing "
			  "the document (bad utf8, bad charset, etc.) "
			  "or any HTTP status error, like 404 or "
			  "505 is included in this count, in addition to "
			  "\"temporary\" errors like DNS timeouts."
			  "</td></tr>"

			  "<tr class=poo><td>hastmperror</td>"
			  "<td>"
			  "This is true if the last spider attempt resulted "
			  "in an error like EDNSTIMEDOUT or a similar error, "
			  "usually indicative of a temporary internet "
			  "failure, or local resource failure, like out of "
			  "memory, and should be retried soon. "
			  "Currently: "
			  "dns timed out, "
			  "tcp timed out, "
			  "dns dead, "
			  "network unreachable, "
			  "host unreachable, "
			  "diffbot internal error, "
			  "out of memory."
			  "</td></tr>"

			  "<tr class=poo><td>percentchangedperday&lt=5</td>"
			  "<td>"
			  "Looks at how much a url's page content has changed "
			  "between the last two times it was spidered, and "
			  "divides that percentage by the number of days. "
			  "So if a URL's last two downloads were 10 days "
			  "apart and its page content changed 30%% then "
			  "the <i>percentchangedperday</i> will be 3. "
			  "Can use <, >, <=, >=, ==, != comparison operators. "
			  "</td></tr>"

			  "<tr class=poo><td>sitenuminlinks&gt;20</td>"
			  "<td>"
			  "How many inlinks does the URL's site have? "
			  "We only count non-spammy inlinks, and at most only "
			  "one inlink per IP address C-Class is counted "
			  "so that a webmaster who owns an entire C-Class "
			  "of IP addresses will only have his inlinks counted "
			  "once."
			  "Can use <, >, <=, >=, ==, != comparison operators. "
			  "</td></tr>"

			  "<tr class=poo><td>httpstatus==404</td>"
			  "<td>"
			  "For matching the URL based on the http status "
			  "of its last download. Does not apply to URLs "
			  "that have not yet been successfully downloaded."
			  "Can use <, >, <=, >=, ==, != comparison operators. "
			  "</td></tr>"

			  /*
			  "<tr class=poo><td>priority==30</td>"
			  "<td>"
			  "<b>If the current priority of the url is 30, then "
			  "it will match this expression. Does not apply "
			  "to outlinks, of course."
			  "</td></tr>"

			  "<tr class=poo><td>parentpriority==30</td>"
			  "<td>"
			  "<b>This is a special expression in that "
			  "it only applies to assigning spider priorities "
			  "to outlinks we are harvesting on a page.</b> "
			  "Matches if the url being added to spider queue "
			  "is from a parent url in priority queue 30. "
			  "The parent's priority queue is the one it got "
			  "moved into while being spidered. So if it was "
			  "in priority 20, but ended up in 25, then 25 will "
			  "be used when scanning the URL Filters table for "
			  "each of its outlinks. Only applies "
			  "to the FIRST time the url is added to spiderdb. "
			  "Use <i>parentpriority==-3</i> to indicate the "
			  "parent was FILTERED and <i>-2</i> to indicate "
			  "the parent was BANNED. A parentpriority of "
			  "<i>-1</i>"
			  " means that the urls is not a link being added to "
			  "spiderdb but rather a url being spidered."
			  "</td></tr>"

			  "<tr class=poo><td>inlink==...</td>"
			  "<td>"
			  "If the url has an inlinker which contains the "
			  "given substring, then this rule is matched. "
			  "We use this like <i>inlink=www.weblogs.com/"
			  "shortChanges.xml</i> to detect if a page is in "
			  "the ping server or not, and if it is, then we "
			  "assign it to a slower-spidering queue, because "
			  "we can reply on the ping server for updates. Saves "
			  "us from having to spider all the blogspot.com "
			  "subdomains a couple times a day each."
			  "</td></tr>"
			  */

			  //"NOTE: Until we get the link info to get the doc "
			  //"quality before calling msg8 in Msg16.cpp, we "
			  //"can not involve doc:quality for purposes of "
			  //"assigning a ruleset, unless banning it.</td>"

			  "<tr class=poo><td><nobr>tld!=com,org,edu"// && "
			  //"doc:quality&lt;70"
			  "</nobr></td>"
			  "<td>Matches if the "
			  "url's TLD does NOT end in \"com\", \"org\" or "
			  "\"edu\". "
			  "</td></tr>"

			  "<tr class=poo><td><nobr>lang==zh_cn,de"
			  "</nobr></td>"
			  "<td>Matches if "
			  "the url's content is in the language \"zh_cn\" or "
			  "\"de\". See table below for supported language "
			  "abbreviations. Used to only keep certain languages "
			  "in the index. This is hacky because the language "
			  "may not be known at spider time, so Gigablast "
			  "will check after downloading the document to "
			  "see if the language <i>spider priority</i> is "
			  "FILTERED or BANNED thereby discarding it.</td></tr>"
			  //"NOTE: Until we move the language "
			  //"detection up before any call to XmlDoc::set1() "
			  //"in Msg16.cpp, we can not use for purposes of "
			  //"assigning a ruleset, unless banning it.</td>"
			  //"</tr>"

			  "<tr class=poo><td><nobr>lang!=xx,en,de"
			  "</nobr></td>"
			  "<td>Matches if "
			  "the url's content is NOT in the language \"xx\" "
			  "(unknown), \"en\" or \"de\". "
			  "See table below for supported language "
			  "abbreviations.</td></tr>"

			  /*
			  "<tr class=poo><td>link:gigablast</td>"
			  "<td>Matches if the document links to gigablast."
			  "</td></tr>"

			  "<tr class=poo><td>searchbox:gigablast</td>"
			  "<td>Matches if the document has a submit form "
			  "to gigablast."
			  "</td></tr>"

			  "<tr class=poo><td>site:dmoz</td>"
			  "<td>Matches if the document is directly or "
			  "indirectly in the DMOZ directory."
			  "</td></tr>"

			  "<tr class=poo><td>tag:spam>X</td>"
			  "<td>Matches if the document's tagdb record "
			  "has a score greater than X for the sitetype, "
			  "'spam' in this case. "
			  "Can use <, >, <=, >=, ==, != comparison operators. "
			  "Other sitetypes include: "
			  "..."
			  "</td></tr>"
			  */

			  "<tr class=poo><td>iswww | !iswww</td>"
			  "<td>Matches if the url's hostname is www or domain "
			  "only. For example: <i>www.xyz.com</i> would match, "
			  "and so would <i>abc.com</i>, but "
			  "<i>foo.somesite.com</i> would NOT match."
			  "</td></tr>"

			  "<tr class=poo><td>isonsamedomain | !isonsamedomain</td>"
			  "<td>"
			  "This is true if the url is from the same "
			  "DOMAIN as the page from which it was "
			  "harvested."
			  //"Only effective for links being added from a page "
			  //"being spidered, because this information is "
			  //"not preserved in the titleRec."
			  "</td></tr>"


			  "<tr class=poo><td><nobr>"
			  "isonsamesubdomain | !isonsamesubdomain"
			  "</nobr></td>"
			  "<td>"
			  "This is true if the url is from the same "
			  "SUBDOMAIN as the page from which it was "
			  "harvested."
			  //"Only effective for links being added from a page "
			  //"being spidered, because this information is "
			  //"not preserved in the titleRec."
			  "</td></tr>"

			  "<tr class=poo><td>ismedia | !ismedia</td>"
			  "<td>"
			  "Does the url have a media or css related "
			  "extension. Like gif, jpg, mpeg, css, etc.? "
			  "</td></tr>"


			  "</td></tr></table><br><br>\n",
			  TABLE_STYLE );


		// show the languages you can use
		sb->safePrintf (
			  "<table %s>"
			  "<tr><td colspan=2><center>"
			  "<b>"
			  "Supported Language Abbreviations "
			  "for lang== Filter</b>"
			  "</td></tr>",
			  TABLE_STYLE );
		for ( long i = 0 ; i < 256 ; i++ ) {
			char *lang1 = getLanguageAbbr   ( i );
			char *lang2 = getLanguageString ( i );
			if ( ! lang1 ) continue;
			sb->safePrintf("<tr class=poo>"
				       "<td>%s</td><td>%s</td></tr>\n",
				      lang1,lang2);
		}
		// wrap it up
		sb->safePrintf("</table><br><br>");

	}

	else  if ( ! isJSON )
		// wrap up the form, print a submit button
		g_pages.printAdminBottom ( sb );

	// extra sync table
	/*
	if ( page == PAGE_SYNC ) {
		// a table that shows the progress of a sync process
		sb.safePrintf (
			  "<br>"
			  "<table width=100%% border=1 bgcolor=#d0d0e0 "
			  "cellpadding=4 border=0>" 
			  //"<tr><td colspan=2 bgcolor=#d0c0d0>"
			  "<tr><td colspan=2 bgcolor=#%s>"
			  "<center>"
			  //"<font size=+1>"
			  "<b>Sync Progress</b>"
			  //"</font>"
			  "</td></tr>\n" , DARK_BLUE);
		
		for ( long i = RDB_START ; i < RDB_END ; i++ ) {
			Rdb *r = getRdbFromId ( i );
			if ( ! r ) continue;
			float pd = g_sync.getPercentDone ( i );
			sb.safePrintf (
				  "<tr>"
				  "<td>%s</td>"
				  "<td>%.1f%%</td></tr>\n",
				  r->m_dbname , pd );
		}
		sb.safePrintf (  "</table>\n");
	}
	*/


	//if ( page == PAGE_FILTERS || page == PAGE_FILTERS2)
	//	g_pages.printRulesetDescriptions ( &sb , user );

	//long plen = p - buf;

	// if just printing into a buffer, return now
	if ( pageBuf ) return true;

	bool POSTReply = g_pages.getPage ( page )->m_usePost;

	return g_httpServer.sendDynamicPage ( s                , 
					      sb->getBufStart() ,
					      sb->length()      , 
					      -1               ,
					      POSTReply        ,
					      NULL             , // contType
					      -1               , // httpstatus
					      cookie           ,
					      NULL             );// charset
}

/*
char *printDropDown ( long n , char *p, char *pend, char *name, long select,
		      bool includeMinusOne ,
		      bool includeMinusTwo ) {
	// begin the drop down menu
	sprintf ( p , "<select name=%s>", name );
	p += gbstrlen ( p );
	char *s;
	long i = -1;
	if ( includeMinusOne ) i = -1;
	// . by default, minus 2 includes minus 3, the new "FILTERED" priority
	// . it is link "BANNED" but does not mean the url is low quality necessarily
	if ( includeMinusTwo ) i = -3;
	for ( ; i < n ; i++ ) {
		if ( i == select ) s = " selected";
		else               s = "";
		if      ( i == -3 ) 
			sprintf (p,"<option value=%li%s>FILTERED",i,s);
		else if ( i == -2 ) 
			sprintf (p,"<option value=%li%s>BANNED",i,s);
		else if ( i == -1 ) 
			sprintf (p,"<option value=%li%s>undefined",i,s);
		else    
			sprintf (p,"<option value=%li%s>%li",i,s,i);
		p += gbstrlen ( p );
	}
	sprintf ( p , "</select>" );
	p += gbstrlen ( p );
	return p;
}

bool printDiffbotDropDown ( SafeBuf *sb,char *name,char *THIS , SafeBuf *sx) {
	//CollectionRec *cr = (CollectionRec *)THIS;
	// . get the string we have selected
	// . the list of available strings to select is in
	//   m_diffbotApiList for this collection, and that can
	//   be changed by john to add custom diffbot api urls.
	// . should just be m_spiderDiffbotApiUrl[i] safebuf
	char *usingApi = sx->getBufStart();
	if ( sx->length() == 0 ) usingApi = NULL;
	// now scan each item in the list. see the setting of
	// "m_def" for "diffbotApiList" below to see the
	// comma separated list of default strings. each item in
	// this list is of the format "<title>|<urlPath>,"
	//char *p = cr->m_diffbotApiList.getBufStart();
	char *p = 
		"None|none,"
		"All|http://www.diffbot.com/api/analyze?mode=auto&fields=*,"
		"Article (autodetect)|http://www.diffbot.com/api/analyze?mode=article&fields=*,"
		"Article (force)|http://www.diffbot.com/api/article?fields=*,"
		"Product (autodetect)|http://www.diffbot.com/api/analyze?mode=product&fields=*,"
		"Product (force)|http://www.diffbot.com/v2/product?fields=*,"
		"Image (autodetect)|http://www.diffbot.com/api/analyze?mode=image&fields=*,"
		"Image (force)|http://www.diffbot.com/api/image?fields=*,"
		"FrontPage (autodetect)|http://www.diffbot.com/api/analyze?mode=frontpage&fields=*,"
		"FrontPage (force)|http://www.diffbot.com/api/frontpage?fields=*"
		;

	// wtf?
	if ( ! p ) return true;
	// print out. cgi is "dapi%li".
	sb->safePrintf("<select name=%s>\n",name);
	// print "none" as the first option
	//char *sel = "";
	//if ( ! usingApi ) sel = " selected";
	//sb->safePrintf("<option value=\"\"%s>None</option>",sel);
	// the various "diffbot urls" are separated by commas
	for ( ; *p ; ) {
		// point to start of item name
		char *name = p;
		// p should now point to name of the item
		char *end1 = p;
		// point to start of url for that item
		for ( ; *end1 && *end1 != '|' ;end1++);
		// save that
		char *url = end1;
		if ( *url == '|' ) url++;
		// find end of url
		char *urlEnd = url;
		for ( ; *urlEnd && *urlEnd != ',' ; urlEnd++ );
		// do we match it?
		char *sel = "";
		if ( usingApi && strncmp(usingApi,url,urlEnd-url)== 0 )
			sel = " selected";
		if ( ! usingApi && urlEnd - url == 0 )
			sel = " selected";
		// advance p
		p = urlEnd;
		// skip over comma to get next one
		if ( *p == ',' ) p++;
		// use the hash as the identifier
		sb->safePrintf("<option value=\"");
		sb->safeMemcpy ( url, urlEnd - url );
		sb->safePrintf("\"%s>",sel);
		// print item name
		sb->safeMemcpy ( name , end1 - name );
		sb->safePrintf("</option>\n");
	}
	sb->safePrintf("</select>");
	return true;
}
*/

bool printDropDown ( long n , SafeBuf* sb, char *name, long select,
		     bool includeMinusOne ,
		     bool includeMinusTwo ) {	// begin the drop down menu
	sb->safePrintf ( "<select name=%s>", name );
	char *s;
	long i = -1;
	if ( includeMinusOne ) i = -1;
	// . by default, minus 2 includes minus 3, the new "FILTERED" priority
	// . it is link "BANNED" but does not mean the url is low quality necessarily
	if ( includeMinusTwo ) i = -3;
	for ( ; i < n ; i++ ) {
		if ( i == select ) s = " selected";
		else               s = "";
		if      ( i == -3 ) 
			sb->safePrintf ("<option value=%li%s>FILTERED",i,s);
		else if ( i == -2 ) 
			sb->safePrintf ("<option value=%li%s>BANNED",i,s);
		else if ( i == -1 ) 
			sb->safePrintf ("<option value=%li%s>undefined",i,s);
		else    
			sb->safePrintf ("<option value=%li%s>%li",i,s,i);
	}
	sb->safePrintf ( "</select>" );
	return true;
}

/*
char *printCheckBoxes ( long n , char *p, char *pend, char *name, char *array){
	for ( long i = 0 ; i < n ; i++ ) {
		if ( i > 0 ) 
			sprintf (p, "<input type=checkbox value=1 name=%s%li",
				 name,i);
		else
			sprintf (p, "<input type=checkbox value=1 name=%s",
				 name);
		p += gbstrlen ( p );
		if ( array[i] ) {
			sprintf ( p , " checked");
			p += gbstrlen ( p );
		}
		sprintf ( p , ">%li &nbsp;" , i );
		p += gbstrlen ( p );
		//if i is single digit, add another nbsp so that everything's 
		//aligned
		if ( i < 10 )
			sprintf(p,"&nbsp;&nbsp;");
		p +=gbstrlen(p);
			
		if ( i > 0 && (i+1) % 6 == 0 )
			sprintf(p,"<br>\n");
		p+=gbstrlen(p);
	}
	return p;
}
*/

bool printCheckBoxes ( long n , SafeBuf* sb, char *name, char *array){
	for ( long i = 0 ; i < n ; i++ ) {
		if ( i > 0 ) 
			sb->safePrintf ("<input type=checkbox value=1 name=%s%li",
					name,i);
		else
			sb->safePrintf ("<input type=checkbox value=1 name=%s",
				 name);
		if ( array[i] ) {
			sb->safePrintf ( " checked");
		}
		sb->safePrintf ( ">%li &nbsp;" , i );
		//if i is single digit, add another nbsp so that everything's 
		//aligned
		if ( i < 10 )
			sb->safePrintf("&nbsp;&nbsp;");
			
		if ( i > 0 && (i+1) % 6 == 0 )
			sb->safePrintf("<br>\n");
	}
	return true;
}

/*
char *Parms::printParms (char *p, char *pend, TcpSocket *s , HttpRequest *r) {
	long  page = g_pages.getDynamicPageNumber ( r );
	//long  user = g_pages.getUserType ( s , r );
	char *username = g_users.getUsername(r);
	char *THIS = getTHIS ( r , page );
	char *coll = r->getString ( "c"   );
	//char *pwd  = r->getString ( "pwd" );
	if ( ! coll ) coll = "";
	//if ( ! pwd  ) pwd  = "";
	long nc = r->getLong("nc",1);
	long pd = r->getLong("pd",1);
	return printParms ( p, pend, page, username, THIS, coll, NULL, nc, pd );
}
*/

bool Parms::printParms (SafeBuf* sb, TcpSocket *s , HttpRequest *r) {
	long  page = g_pages.getDynamicPageNumber ( r );
	//long  user = g_pages.getUserType ( s , r );
	//char *username = g_users.getUsername(r);
	char *THIS = getTHIS ( r , page );
	char *coll = r->getString ( "c"   );
	//char *pwd  = r->getString ( "pwd" );
	if ( ! coll ) coll = "";
	//if ( ! pwd  ) pwd  = "";
	long nc = r->getLong("nc",1);
	long pd = r->getLong("pd",1);
	return printParms ( sb, page, NULL, THIS, coll, NULL, nc, pd );
}

static long s_count = 0;

/*
// . we just start printing with <tr><td>... assuming caller has already 
//   printed a <table> into "p"
// . return "p" after printing into it
char *Parms::printParms ( char *p , char *pend , long page , char *username,
                          //long user ,
			  void *THIS , char *coll , char *pwd , long nc ,
			  long pd ) {
	s_count = 0;
	// background color
	char *bg1 = LIGHT_BLUE;
	char *bg2 = DARK_BLUE;
	// find in parms list
	for ( long i = 0 ; i < m_numParms ; i++ ) {
		// get it
		Parm *m = &m_parms[i];
		// make sure we got the right parms for what we want
		if ( m->m_page != page ) continue;
		// skip if offset is negative, that means none
		if ( m->m_off < 0 &&
		     m->m_type != TYPE_CMD      &&
		     m->m_type != TYPE_CONSTANT   ) continue;
		// might have an array, do not exceed the array size
		long  jend = m->m_max;
		long  size = jend ;
		char *ss   = ((char *)THIS + m->m_off - 4);
		if ( m->m_max > 1 ) size = *(long *)ss;
		if ( size < jend  ) jend = size;
		// background color
		char *bg ;
		// toggle background color on group boundaries...
		if ( m->m_group == 1 ) {
			if ( bg == bg1 ) bg = bg2;
			else             bg = bg1;
		}
		// . do we have an array? if so print title on next row
		//   UNLESS these are priority checkboxes, those can all 
		//   cluster together onto one row
		// . only add if not in a row of controls
		if ( m->m_max > 1 && m->m_type != TYPE_PRIORITY_BOXES &&
		     m->m_rowid == -1 ) {
			// make a separate table for array of parms
			sprintf ( p , 
				  //"<table width=100%% bgcolor=#d0d0e0 "
				  //"cellpadding=4 border=1>\n"
				  "<tr><td colspan=20 bgcolor=#%s>"
				  "<center>"
				  //"<font size=+1>"
				  "<b>%s"
				  "</b>"
				  //"</font>"
				  "</td></tr>\n"
				  "<tr><td colspan=20><font size=1>"
				  "%s</font></td></tr>\n",
				  DARK_BLUE,m->m_title,m->m_desc );
			p += gbstrlen ( p );
		}
		// arrays always have blank line for adding stuff
		if ( m->m_max > 1 ) size++;
		// if m_rowid of consecutive parms are the same then they
		// are all printed in the same row, otherwise the inner loop
		// has no effect
		long rowid = m_parms[i].m_rowid;
		// if not part of a complex row, just print this array right up
		if ( rowid == -1 ) {
			for ( long j = 0 ; j < size ; j++ )
				p=printParm ( p, pend, username, &m_parms[i],i,
					      j, jend, (char *)THIS,coll,NULL,
					      bg,nc,pd);
			continue;
		}
		// if not first in a row, skip it, we printed it already
		if ( i > 0 && m_parms[i-1].m_rowid == rowid ) continue;

		// otherwise print everything in the row
		for ( long j = 0 ; j < size ; j++ ) {
			for ( long k = i ; 
			      k < m_numParms && 
				      m_parms[k].m_rowid == rowid;  
			      k++ )
				p=printParm(p,pend,username,&m_parms[k],k,j,
					    jend,(char *)THIS,coll,NULL,bg,nc,
					    pd);
		}
		// end array table
		//if ( m->m_max > 1 ) {
		//	sprintf ( p , "</table><br>\n");
		//	p += gbstrlen ( p );
		//}
	}
	return p;
}
*/
bool Parms::printParms ( SafeBuf* sb , long page , char *username,//long user,
			 void *THIS , char *coll , char *pwd , long nc ,
			 long pd , bool isCrawlbot , bool isJSON ) {
	bool status = true;
	s_count = 0;
	// background color
	char *bg1 = LIGHT_BLUE;
	char *bg2 = DARK_BLUE;
	// background color
	char *bg = NULL;

	// page aliases
	if ( page == PAGE_BASIC_PASSWORDS )
		page = PAGE_SECURITY;

	// find in parms list
	for ( long i = 0 ; i < m_numParms ; i++ ) {
		// get it
		Parm *m = &m_parms[i];
		// make sure we got the right parms for what we want
		if ( m->m_page != page ) continue;
		// skip if offset is negative, that means none
		if ( m->m_off < 0 && 
		     m->m_type != TYPE_MONOD2 &&
		     m->m_type != TYPE_MONOM2 &&
		     m->m_type != TYPE_CMD     ) continue;
		// skip if hidden
		if ( m->m_flags & PF_HIDDEN ) continue;
		// might have an array, do not exceed the array size
		long  jend = m->m_max;
		long  size = jend ;
		char *ss   = ((char *)THIS + m->m_off - 4);
		if ( m->m_type == TYPE_MONOD2 ) ss = NULL;
		if ( m->m_type == TYPE_MONOM2 ) ss = NULL;
		if ( m->m_max > 1 && ss ) size = *(long *)ss;
		if ( size < jend        ) jend = size;
		// toggle background color on group boundaries...
		if ( m->m_group == 1 ) {
			if ( bg == bg1 ) bg = bg2;
			else             bg = bg1;
		}

			//
			// mdw just debug to here ... left off here
			//char *xx=NULL;*xx=0;

		// . do we have an array? if so print title on next row
		//   UNLESS these are priority checkboxes, those can all 
		//   cluster together onto one row
		// . only add if not in a row of controls
		if ( m->m_max > 1 && m->m_type != TYPE_PRIORITY_BOXES &&
		     m->m_rowid == -1 &&
		     ! isJSON ) {
			//
			// make a separate table for array of parms
			sb->safePrintf (
				  //"<table width=100%% bgcolor=#d0d0e0 "
				  //"cellpadding=4 border=1>\n"
				  "<tr><td colspan=20 bgcolor=#%s>"
				  "<center>"
				  //"<font size=+1>"
				  "<b>%s"
				  "</b>"
				  //"</font>"
				  "</td></tr>\n"
				  "<tr><td colspan=20><font size=-1>"
				  "%s</font></td></tr>\n",
				  DARK_BLUE,m->m_title,m->m_desc );
		}
		// arrays always have blank line for adding stuff
		if ( m->m_max > 1 )
		     // not for PAGE_PRIORITIES!
		     //m->m_page != PAGE_PRIORITIES )
			size++;
		// if m_rowid of consecutive parms are the same then they
		// are all printed in the same row, otherwise the inner loop
		// has no effect
		long rowid = m_parms[i].m_rowid;
		// if not part of a complex row, just print this array right up
		if ( rowid == -1 ) {
			for ( long j = 0 ; j < size ; j++ )
				status &=printParm ( sb,username,&m_parms[i],i,
						     j, jend, (char *)THIS,
						     coll,NULL,
						     bg,nc,pd,
						     false,
						     isCrawlbot,
						     isJSON);
			continue;
		}
		// if not first in a row, skip it, we printed it already
		if ( i > 0 && m_parms[i-1].m_rowid == rowid ) continue;

		// otherwise print everything in the row
		for ( long j = 0 ; j < size ; j++ ) {
			// flip j if in this page
			long newj = j;
			//if ( m->m_page == PAGE_PRIORITIES )
			//	newj = size - 1 - j;
			for ( long k = i ; 
			      k < m_numParms && 
				      m_parms[k].m_rowid == rowid;  
			      k++ ) {

				status &=printParm(sb,username,&m_parms[k],k,
					    newj,jend,(char *)THIS,coll,NULL,
						   bg,nc,pd, j==size-1,
						   isCrawlbot,isJSON);
			}
		}
		// end array table
		//if ( m->m_max > 1 ) {
		//	sprintf ( p , "</table><br>\n");
		//	p += gbstrlen ( p );
		//}
	}
	return status;
}

/*
char *Parms::printParm ( char *p    , 
			 char *pend ,
			 //long  user ,
			 char *username,
			 Parm *m    , 
			 long  mm   , // m = &m_parms[mm]
			 long  j    ,
			 long  jend ,
			 char *THIS ,
			 char *coll ,
			 char *pwd  ,
			 char *bg   ,
			 long  nc   ,
			 long  pd   ) {
	// do not print if no permissions
	if ( m->m_perms != 0 && !g_users.hasPermission(username,m->m_perms) )
		return p;
	//if ( m->m_perms != 0 && (m->m_perms & user) == 0 ) return p;
	// do not print some if #define _CLIENT_ is true
#ifdef _GLOBALSPEC_
	if ( m->m_priv == 2 ) return p;
	if ( m->m_priv == 3 ) return p;
#elif _CLIENT_
	if ( m->m_priv ) return p;
#elif _METALINCS_
	if ( m->m_priv == 2 ) return p;
	if ( m->m_priv == 3 ) return p;
#endif
	// priv of 4 means do not print at all
	if ( m->m_priv == 4 ) return p;

	// what type of parameter?
	char t = m->m_type;
	// point to the data in THIS
	char *s = THIS + m->m_off + m->m_size * j ;
	// . if an array, passed our end, this is the blank line at the end
	// . USE THIS EMPTY/DEFAULT LINE TO ADD NEW DATA TO AN ARRAY
	// . make at least as big as a long long
	if ( j >= jend ) s = "\0\0\0\0\0\0\0\0";
	// delimit each cgi var if we need to
	if ( m->m_cgi && gbstrlen(m->m_cgi) > 45 ) {
		log(LOG_LOGIC,"admin: Cgi variable is TOO big.");
		char *xx = NULL; *xx = 0;
	}
	char cgi[64];
	if ( m->m_cgi ) {
		if ( j > 0 ) sprintf ( cgi , "%s%li" , m->m_cgi , j );
		else         sprintf ( cgi , "%s"    , m->m_cgi     );
	}
	// . display title and description of the control/parameter
	// . the input cell of some parameters are colored
	char *color = "";
	if ( t == TYPE_CMD  || t == TYPE_BOOL2 ) color = " bgcolor=#0000ff";
	if ( t == TYPE_BOOL ) {
		if ( *s ) color = " bgcolor=#00ff00";
		else      color = " bgcolor=#ff0000";
	}
	if ( t == TYPE_BOOL || t == TYPE_BOOL2 ) {
		// disable controls not allowed in read only mode
		if ( g_conf.m_readOnlyMode && m->m_rdonly )
			  color = " bgcolor=#ffff00";
	}

	bool firstInRow = false;
	if ( (s_count % nc) == 0 ) firstInRow = true;
	s_count++;

	if ( mm > 0 && m->m_rowid >= 0 && m_parms[mm-1].m_rowid == m->m_rowid )
		firstInRow = false;
	long firstRow = 0;
	if ( m->m_page == PAGE_PRIORITIES ) firstRow = MAX_PRIORITY_QUEUES - 1;
	// . use a separate table for arrays
	// . make title and description header of that table
	// . do not print all headers if not m_hdrs, a special case for the 
	//   default line in the url filters table
	if ( j == firstRow && m->m_rowid >= 0 && firstInRow && m->m_hdrs ) {
		// print description as big comment
		if ( m->m_desc && pd == 1 ) {
			sprintf ( p , "<td colspan=20><font size=1>\n" );
			p += gbstrlen ( p );
			//p = htmlEncode ( p , pend , m->m_desc ,
			//		 m->m_desc + gbstrlen ( m->m_desc ) );
			sprintf ( p , "%s" , m->m_desc );
			p += gbstrlen ( p );
			sprintf ( p , "</font></td></tr><tr>\n" );
			p += gbstrlen ( p );
		}
		// # column
		// do not show this for PAGE_PRIORITIES it is confusing
		if ( m->m_max > 1 && 
		     m->m_page != PAGE_PRIORITIES ) {
			sprintf ( p , "<td><b>#</b></td>\n" );
			p += gbstrlen(p);
		}
		// print all headers
		for ( long k = mm ; 
		      k<m_numParms && m_parms[k].m_rowid==m->m_rowid; k++ ) {
			sprintf ( p , "<td><b>%s</b></td>\n" ,
				  m_parms[k].m_title );
			p += gbstrlen(p);
		}
		sprintf ( p , "</tr>\n" ); // mdw added
		p += gbstrlen ( p ); 
	}
	// print row start for single parm
	if ( m->m_max <= 1 && ! m->m_hdrs ) {
		if ( firstInRow ) {
			sprintf ( p , "<tr bgcolor=#%s><td>" , bg );
			p += gbstrlen ( p );
		}
		p += sprintf ( p , "<td width=%li%%>" , 100/nc/2 );
	}

	// print the title/description in current table for non-arrays
	if ( m->m_max <= 1 && m->m_hdrs ) { // j == 0 && m->m_rowid < 0 ) {
		if ( firstInRow )
			p += sprintf ( p , "<tr bgcolor=#%s>",bg);
		if ( t == TYPE_STRINGBOX ) {
			sprintf ( p , "<td colspan=2><center>"
				  "<b>%s</b><br><font size=1>",m->m_title );
			p += gbstrlen ( p );
			if ( pd ) 
				p = htmlEncode (p,pend,m->m_desc,
						m->m_desc+gbstrlen(m->m_desc));
			sprintf ( p , "</font><br>\n" );
		}
		else {
			sprintf ( p , "<td width=%li%%>" //"<td width=78%%>"
				  "<b>%s</b><br><font size=1>",
				  3*100/nc/2/4,m->m_title );
			p += gbstrlen ( p );
			if ( pd ) 
				p  = htmlEncode (p,pend,m->m_desc,
						 m->m_desc+gbstrlen(m->m_desc));
			// and default value if it exists
			if ( m->m_def && m->m_def[0] && t != TYPE_CMD ) {
				char *d = m->m_def;
				if ( t == TYPE_BOOL ) {
					if ( d[0]=='0' ) d = "NO";
					else             d = "YES";
					sprintf ( p , " Default: %s.",d);
					p += gbstrlen ( p );
				} 
				else {
					sprintf ( p , " Default: ");
					p += gbstrlen ( p );
					p = htmlEncode (p,pend,d,d+gbstrlen(d) );
				}
			}
			sprintf ( p , "</font></td>\n<td%s width=%li%%>" , 
				  color , 100/nc/2/4 );
		}
		p += gbstrlen ( p );
	}

	// . print number in row if array, start at 1 for clarity's sake
	// . used for url filters table, etc.
	if ( m->m_max > 1 ) {
		// but if it is in same row as previous, do not repeat it
		// for this same row, silly
		if ( firstInRow && m->m_page != PAGE_PRIORITIES ) 
			sprintf ( p, "<tr><td>%li</td>\n<td>", j);//j+1 );
		else if ( firstInRow ) 
			sprintf ( p , "<tr><td>" );
		else    
			sprintf ( p, "<td>" );
		p += gbstrlen ( p );
	}

	long cast = m->m_cast;
	if ( g_proxy.isProxy() ) cast = 0;

	// print the input box
	if ( t == TYPE_BOOL ) {
		char *tt, *v;
		if ( *s ) { tt = "YES"; v = "0"; }
		else      { tt = "NO" ; v = "1"; }
		if ( g_conf.m_readOnlyMode && m->m_rdonly )
			sprintf ( p, "<b>read-only mode</b>" );
		// if cast=1, command IS broadcast to all hosts
		else 
			sprintf ( p, "<b><a href=\"/%s?c=%s&"
				  "%s=%s&cast=%li\">"
				  "<center>%s</center></a></b>", 
				  g_pages.getPath(m->m_page),coll,
				  cgi,v,cast,tt);
	}
	else if ( t == TYPE_BOOL2 ) {
		if ( g_conf.m_readOnlyMode && m->m_rdonly )
			sprintf ( p, "<b><center>read-only mode</center></b>");
		// always use m_def as the value for TYPE_BOOL2
		else
			sprintf ( p, "<b><a href=\"/%s?c=%s&%s=%s&"
				  "cast=1\">"
				  "<center>%s</center></a></b>", 
				  g_pages.getPath(m->m_page),coll,
				  cgi,m->m_def, m->m_title);
	}
	else if ( t == TYPE_CHECKBOX ) {
		char *ddd = "";
		if ( *s ) ddd = " checked";
		sprintf (p, "<input type=checkbox value=1 name=%s"
			 "%s>",
			 cgi,ddd);
	}
	else if ( t == TYPE_CHAR )
		sprintf (p,"<input type=text name=%s value=\"%li\" "
			 "size=3>",cgi,(long)(*s));
	else if ( t == TYPE_PRIORITY ) 
		printDropDown ( MAX_SPIDER_PRIORITIES , p , pend , cgi , *s , 
				false , false );
	else if ( t == TYPE_PRIORITY2 )
		printDropDown ( MAX_SPIDER_PRIORITIES , p , pend , cgi , *s , 
				true , true );
	else if ( t == TYPE_RETRIES    ) 
		printDropDown ( 4 , p , pend , cgi , *s , false , false );
	else if ( t == TYPE_PRIORITY_BOXES ) {
		// print ALL the checkboxes when we get the first parm
		if ( j != 0 ) return p;
		printCheckBoxes ( MAX_SPIDER_PRIORITIES , p , pend , cgi , s );
	}
	else if ( t == TYPE_CMD )
		// if cast=0 it will be executed, otherwise it will be
		// broadcasted with cast=1 to all hosts and they will all
		// execute it
		sprintf ( p, "<b><a href=\"/%s?c=%s&%s=1&cast=%li\">"
			  "<center>%s</center></a></b>",
			  g_pages.getPath(m->m_page),coll,
			  cgi,cast,m->m_title);
	else if ( t == TYPE_FLOAT )
		sprintf (p,"<input type=text name=%s value=\"%.03f\" "
			 "size=12>",cgi,*(float *)s);
	else if ( t == TYPE_IP ) {
		if ( m->m_max > 0 && j == jend ) 
			sprintf (p,"<input type=text name=%s value=\"\" "
				 "size=12>",cgi);
		else
			sprintf (p,"<input type=text name=%s value=\"%s\" "
				 "size=12>",cgi,iptoa(*(long *)s));
	}
	else if ( t == TYPE_LONG ) 
		sprintf (p,"<input type=text name=%s value=\"%li\" "
			 "size=12>",cgi,*(long *)s);
	else if ( t == TYPE_LONG_CONST ) 
		sprintf (p,"%li",*(long *)s);
	else if ( t == TYPE_LONG_LONG )
		sprintf (p,"<input type=text name=%s value=\"%lli\" "
			 "size=32>",cgi,*(long long *)s);
	else if ( t == TYPE_STRING || t == TYPE_STRINGNONEMPTY ) {
		long size = m->m_size;
		if ( size > 25 ) size = 25;
		sprintf (p,"<input type=text name=%s size=%li value=\"",
			 cgi,size);
		p += gbstrlen(p);
		p += dequote ( p , pend , s , gbstrlen(s) );
		sprintf (p,"\">");
	}
	else if ( t == TYPE_STRINGBOX ) {
		sprintf(p,"<textarea rows=10 cols=64 name=%s>",cgi);
		p += gbstrlen(p);
		//p += urlEncode ( p , pend - p , s , gbstrlen(s) );
		//p += htmlDecode ( p , s , gbstrlen(s) );
		p = htmlEncode ( p , pend , s , s + gbstrlen(s) );
		//sprintf ( p , "%s" , s );
		//p += gbstrlen(p);		
		sprintf (p,"</textarea>\n");
	}
	else if ( t == TYPE_CONSTANT ) 
		sprintf (p,"%s",m->m_title);
	else if ( t == TYPE_MONOD2 )
		sprintf ( p , "%li" , j / 2 );
	else if ( t == TYPE_MONOM2 ) 
		sprintf ( p , "%li" , j % 2 );
	else if ( t == TYPE_RULESET ) ;
		// subscript is already included in "cgi"
		//p = g_pages.printRulesetDropDown ( p          ,
		//				   pend       ,
		//				   user       ,
		//				   cgi        ,
		//				   *(long *)s ,  // selected  
		//				   -1         ); // subscript
	else if ( t == TYPE_TIME ) {
		//time is stored as a string
		//if time is not stored properly, just write 00:00
		if ( s[2] != ':' )
			strncpy ( s, "00:00", 5 );
		char hr[3];
		char min[3];
		memcpy ( hr, s, 2 );
		memcpy ( min, s + 3, 2 );
		hr[2] = '\0';
		min[2] = '\0';
		// print the time in the input forms
		sprintf(p,
			"<input type=text name=%shr size=2 "
			"value=%s>h " 
			"<input type=text name=%smin size=2 "
			"value=%s>m " ,
			cgi    , 
			hr ,
			cgi    , 
			min  );
	}

	else if ( t == TYPE_DATE || t == TYPE_DATE2 ) {
		// time is stored as long
		long ct = *(long *)s;
		// get the time struct
		struct tm *tp = gmtime ( (time_t *)&ct ) ;
		// set the "selected" month for the drop down
		char *ss[12];
		for ( long i = 0 ; i < 12 ; i++ ) ss[i]="";
		long month = tp->tm_mon;
		if ( month < 0 || month > 11 ) month = 0; // Jan
		ss[month] = " selected";
		// print the date in the input forms
		sprintf(p,
			"<input type=text name=%sday "
			"size=2 value=%li> "
			"<select name=%smon>"
			"<option value=0%s>Jan"
			"<option value=1%s>Feb"
			"<option value=2%s>Mar"
			"<option value=3%s>Apr"
			"<option value=4%s>May"
			"<option value=5%s>Jun"
			"<option value=6%s>Jul"
			"<option value=7%s>Aug"
			"<option value=8%s>Sep"
			"<option value=9%s>Oct"
			"<option value=10%s>Nov"
			"<option value=11%s>Dec"
			"</select>\n"
			"<input type=text name=%syr size=4 value=%li>"
			"<br>"
			"<input type=text name=%shr size=2 "
			"value=%02li>h " 
			"<input type=text name=%smin size=2 "
			"value=%02li>m " 
			"<input type=text name=%ssec size=2 "
			"value=%02li>s" ,
			cgi    ,
			(long)tp->tm_mday ,
			cgi    ,
			ss[0],ss[1],ss[2],ss[3],ss[4],ss[5],ss[6],ss[7],ss[8],
			ss[9],ss[10],ss[11],
			cgi    ,
			(long)tp->tm_year + 1900 ,
			cgi    , 
			(long)tp->tm_hour ,
			cgi    , 
			(long)tp->tm_min  ,
			cgi    ,
			(long)tp->tm_sec  );
	}
	else if ( t == TYPE_SITERULE ) {
		// print the siterec rules as a drop down
		char *ss[5];
		for ( long i = 0; i < 5; i++ ) ss[i] = "";
		long v = *(long*)s;
		if ( v < 0 || v > 4 ) v = 0;
		ss[v] = " selected";
		sprintf ( p, "<select name=%s>"
			     "<option value=0%s>Hostname"
			     "<option value=1%s>Path Depth 1"
			     "<option value=2%s>Path Depth 2"
			     "<option value=3%s>Path Depth 3"
			     "</select>\n",
			     cgi, ss[0], ss[1], ss[2], ss[3] );
	}

	p += gbstrlen ( p );

	// end the input cell
	sprintf ( p , "</td>\n");
	p += gbstrlen ( p );

	// "insert above" link? used for arrays only, where order matters
	if ( m->m_addin && j < jend ) {
		sprintf ( p , "<td><a href=\"?c=%s&cast=1&"
			  "ins_%s=1\">insert</td>\n",coll,cgi );
		p += gbstrlen ( p );
	}

	// does next guy start a new row?
	bool lastInRow = true; // assume yes
	if ( mm+1<m_numParms&&m->m_rowid>=0&&m_parms[mm+1].m_rowid==m->m_rowid)
		lastInRow = false;
	if ( ((s_count-1) % nc) != (nc-1) ) lastInRow = false;

	// . display the remove link for arrays if we need to
	// . but don't display if next guy does NOT start a new row
	if ( m->m_max > 1 && lastInRow &&
	     m->m_page != PAGE_PRIORITIES ) {
		if ( j < jend  )
			sprintf ( p , "<td><a href=\"?c=%s&cast=1&"
				  "rm_%s=1\">"
				  "remove</td>\n",coll,cgi );
		else
			sprintf ( p , "<td></td>\n");
		p += gbstrlen ( p );
	}

	if ( lastInRow ) sprintf ( p , "</tr>\n");
	p += gbstrlen ( p );

	return p;
}
*/

bool Parms::printParm ( SafeBuf* sb,
			//long  user ,
			char *username,
			Parm *m    , 
			long  mm   , // m = &m_parms[mm]
			long  j    ,
			long  jend ,
			char *THIS ,
			char *coll ,
			char *pwd  ,
			char *bg   ,
			long  nc   ,
			long  pd   ,
			bool lastRow ,
			bool isCrawlbot ,
			bool isJSON ) {
	bool status = true;
	// do not print if no permissions
	if ( m->m_perms != 0 && !g_users.hasPermission(username,m->m_perms) )
		return status;
	//if ( m->m_perms != 0 && (m->m_perms & user) == 0 ) return status;
	// do not print some if #define _CLIENT_ is true
#ifdef _GLOBALSPEC_
	if ( m->m_priv == 2 ) return status;
	if ( m->m_priv == 3 ) return status;
#elif _CLIENT_
	if ( m->m_priv ) return status;
#elif _METALINCS_
	if ( m->m_priv == 2 ) return status;
	if ( m->m_priv == 3 ) return status;
#endif
	// priv of 4 means do not print at all
	if ( m->m_priv == 4 ) return status;

	if ( m->m_flags & PF_HIDDEN ) return status;

	// . if printing on crawlbot page hide these
	// . we repeat this logic below when printing parm titles
	//   for the column headers in the table
	//char *vt = "";
	//if ( isCrawlbot &&
	//     m->m_page == PAGE_FILTERS &&
	//     (strcmp(m->m_xml,"spidersEnabled") == 0 ||
	//      //strcmp(m->m_xml,"maxSpidersPerRule")==0||
	//      //strcmp(m->m_xml,"maxSpidersPerIp") == 0||
	//      strcmp(m->m_xml,"spiderIpWait") == 0 
	//      ) )
	//	vt = " style=display:none;";

	// what type of parameter?
	char t = m->m_type;
	// point to the data in THIS
	char *s = THIS + m->m_off + m->m_size * j ;
	// . if an array, passed our end, this is the blank line at the end
	// . USE THIS EMPTY/DEFAULT LINE TO ADD NEW DATA TO AN ARRAY
	// . make at least as big as a long long
	if ( j >= jend ) s = "\0\0\0\0\0\0\0\0";
	// delimit each cgi var if we need to
	if ( m->m_cgi && gbstrlen(m->m_cgi) > 45 ) {
		log(LOG_LOGIC,"admin: Cgi variable is TOO big.");
		char *xx = NULL; *xx = 0;
	}
	char cgi[64];
	if ( m->m_cgi ) {
		if ( j > 0 ) sprintf ( cgi , "%s%li" , m->m_cgi , j );
		else         sprintf ( cgi , "%s"    , m->m_cgi     );
		// let's try dropping the index # and just doing dup parms
		//sprintf ( cgi , "%s"    , m->m_cgi     );
	}
	// . display title and description of the control/parameter
	// . the input cell of some parameters are colored
	char *color = "";
	if ( t == TYPE_CMD  || t == TYPE_BOOL2 ) 
		color = " bgcolor=#6060ff";
	if ( t == TYPE_BOOL ) {
		if ( *s ) color = " bgcolor=#00ff00";
		else      color = " bgcolor=#ff0000";
	}
	if ( t == TYPE_BOOL || t == TYPE_BOOL2 ) {
		// disable controls not allowed in read only mode
		if ( g_conf.m_readOnlyMode && m->m_rdonly )
			  color = " bgcolor=#ffff00";
	}

	bool firstInRow = false;
	if ( (s_count % nc) == 0 ) firstInRow = true;
	s_count++;

	if ( mm > 0 && m->m_rowid >= 0 && m_parms[mm-1].m_rowid == m->m_rowid )
		firstInRow = false;

	CollectionRec *cr = NULL;
	collnum_t collnum = -1;
	if ( coll ) {
		cr = g_collectiondb.getRec ( coll );
		if ( cr ) collnum = cr->m_collnum;
	}

	long firstRow = 0;
	//if ( m->m_page==PAGE_PRIORITIES ) firstRow = MAX_PRIORITY_QUEUES - 1;
	// . use a separate table for arrays
	// . make title and description header of that table
	// . do not print all headers if not m_hdrs, a special case for the 
	//   default line in the url filters table
	if ( j == firstRow && m->m_rowid >= 0 && firstInRow && m->m_hdrs ) {
		// print description as big comment
		if ( m->m_desc && pd == 1 && ! isJSON ) {
			// url FILTERS table description row
			sb->safePrintf ( "<td colspan=20 bgcolor=#%s>"
					 "<font size=-1>\n" , DARK_BLUE);

			//p = htmlEncode ( p , pend , m->m_desc ,
			//		 m->m_desc + gbstrlen ( m->m_desc ) );
			sb->safePrintf ( "%s" , m->m_desc );
			sb->safePrintf ( "</font></td></tr>"
					 // for "#,expression,harvestlinks.."
					 // header row in url FILTERS table
					 "<tr bgcolor=#%s>\n" ,DARK_BLUE);
		}
		// # column
		// do not show this for PAGE_PRIORITIES it is confusing
		if ( m->m_max > 1 && ! isJSON ) {
		     //m->m_page != PAGE_PRIORITIES ) {
			sb->safePrintf (  "<td><b>#</b></td>\n" );
		}
		// print all headers
		for ( long k = mm ; 
		      k<m_numParms && m_parms[k].m_rowid==m->m_rowid; k++ ) {
			// parm shortcut
			Parm *mk = &m_parms[k];
			// not if printing json
			if ( isJSON ) continue;

			// skip if hidden
			if ( cr && ! cr->m_isCustomCrawl &&
			     (mk->m_flags & PF_DIFFBOT) )
				continue;

			// . hide table column headers that are too advanced
			// . we repeat this logic above for the actual parms
			//char *vt = "";
			//if ( isCrawlbot &&
			//     m->m_page == PAGE_FILTERS &&
			//     (strcmp(mk->m_xml,"spidersEnabled") == 0 ||
			//      //strcmp(mk->m_xml,"maxSpidersPerRule")==0||
			//      //strcmp(mk->m_xml,"maxSpidersPerIp") == 0||
			//      strcmp(mk->m_xml,"spiderIpWait") == 0 ) )
			//	vt = " style=display:none;display:none;";
			//sb->safePrintf ( "<td%s>" , vt );
			sb->safePrintf ( "<td>" );
			// if its of type checkbox in a table make it
			// toggle them all on/off
			if ( mk->m_type == TYPE_CHECKBOX &&
			     mk->m_page == PAGE_FILTERS ) {
				sb->safePrintf("<a href=# "
					       "onclick=\"checkAll(this, "
					       "'id_%s', %li);\">",
					       m_parms[k].m_cgi, m->m_max);
			}
			sb->safePrintf ( "<b>%s</b>", m_parms[k].m_title );
			if ( mk->m_type == TYPE_CHECKBOX &&
			     mk->m_page == PAGE_FILTERS ) 
				sb->safePrintf("</a>");
			/*
			if ( m->m_page == PAGE_PRIORITIES && 
			     m_parms[k].m_type == TYPE_CHECKBOX)
				sb->safePrintf("<br><a href=# "
					       "onclick=\"checkAll(this, "
					       "'id_%s', %li);\">(toggle)</a>",
					       m_parms[k].m_cgi, m->m_max);
			*/
			sb->safePrintf ("</td>\n");
		}
		if ( ! isJSON ) sb->safePrintf ( "</tr>\n" ); // mdw added
	}

	// skip if hidden. diffbot api url only for custom crawls.
	//if(cr && ! cr->m_isCustomCrawl && (m->m_flags & PF_DIFFBOT) )
	//	return true;

	// print row start for single parm
	if ( m->m_max <= 1 && ! m->m_hdrs ) {
		if ( firstInRow ) {
			sb->safePrintf ( "<tr bgcolor=#%s><td>" , bg );
		}
		sb->safePrintf ( "<td width=%li%%>" , 100/nc/2 );
	}

	// if parm value is not defaut, use orange!
	char rr[1024];
	SafeBuf val1(rr,1024);
	m->printVal ( &val1 , collnum , j ); // occNum );
	// test it
	if ( m->m_def && strcmp ( val1.getBufStart() , m->m_def ) )
		// put non-default valued parms in orange!
		bg = "ffa500";


	// print the title/description in current table for non-arrays
	if ( m->m_max <= 1 && m->m_hdrs ) { // j == 0 && m->m_rowid < 0 ) {
		if ( firstInRow )
			sb->safePrintf ( "<tr bgcolor=#%s>",bg);
		if ( t == TYPE_STRINGBOX ) {
			sb->safePrintf ( "<td colspan=2><center>"
				  "<b>%s</b><br><font size=-1>",m->m_title );
			if ( pd ) 
				status &= sb->htmlEncode (m->m_desc,
							  gbstrlen(m->m_desc),
							  false);
			sb->safePrintf ( "</font><br>\n" );
		}
		else {
			// this td will be invisible if isCrawlbot and the
			// parm is too advanced to display
			sb->safePrintf ( "<td width=%li%%>"//"<td width=78%%>
					 "<b>%s</b><br><font size=1>",
					 3*100/nc/2/4, m->m_title );
			if ( pd ) 
				status &= sb->htmlEncode (m->m_desc,
							  gbstrlen(m->m_desc),
							  false);
			// and cgi parm if it exists
			if ( m->m_def && m->m_scgi )
				sb->safePrintf(" CGI override: %s.",m->m_scgi);
			// and default value if it exists
			if ( m->m_def && m->m_def[0] && t != TYPE_CMD ) {
				char *d = m->m_def;
				if ( t == TYPE_BOOL ) {
					if ( d[0]=='0' ) d = "NO";
					else             d = "YES";
					sb->safePrintf ( " Default: %s.",d);
				} 
				else {
					sb->safePrintf (" Default: ");
					status &= sb->htmlEncode (d,
								  gbstrlen(d),
								  false);
				}
			}
			sb->safePrintf ( "</font></td>\n<td%s width=%li%%>" , 
				  color , 100/nc/2/4 );
		}
	}

	// . print number in row if array, start at 1 for clarity's sake
	// . used for url filters table, etc.
	if ( m->m_max > 1 ) {
		// bg color alternates
		char *bgc = LIGHT_BLUE;
		if ( j % 2 ) bgc = DARK_BLUE;
		// do not print this if doing json
		if ( isJSON ) ;
		// but if it is in same row as previous, do not repeat it
		// for this same row, silly
		else if ( firstInRow ) // && m->m_page != PAGE_PRIORITIES ) 
			sb->safePrintf ( "<tr bgcolor=#%s>"
					 "<td>%li</td>\n<td>", 
					 bgc,
					 j );//j+1
		else if ( firstInRow ) 
			sb->safePrintf ( "<tr><td>" );
		else    
			//sb->safePrintf ( "<td%s>" , vt);
			sb->safePrintf ( "<td>" );
	}

	long cast = m->m_cast;
	if ( g_proxy.isProxy() ) cast = 0;

	// print the input box
	if ( t == TYPE_BOOL ) {
		char *tt, *v;
		if ( *s ) { tt = "YES"; v = "0"; }
		else      { tt = "NO" ; v = "1"; }
		if ( g_conf.m_readOnlyMode && m->m_rdonly )
			sb->safePrintf ( "<b>read-only mode</b>" );
		// if cast=1, command IS broadcast to all hosts
		else 
			sb->safePrintf ( "<b><a href=\"/%s?c=%s&"
					 "%s=%s&cast=%li\">"
					 "<center>%s</center></a></b>", 
					 g_pages.getPath(m->m_page),coll,
					 cgi,v,cast,tt);
	}
	else if ( t == TYPE_BOOL2 ) {
		if ( g_conf.m_readOnlyMode && m->m_rdonly )
			sb->safePrintf ( "<b><center>read-only mode</center></b>");
		// always use m_def as the value for TYPE_BOOL2
		else
			sb->safePrintf ( "<b><a href=\"/%s?c=%s&%s=%s&"
					 "cast=1\">"
					 "<center>%s</center></a></b>", 
					 g_pages.getPath(m->m_page),coll,
					 cgi,m->m_def, m->m_title);
	}
	else if ( t == TYPE_CHECKBOX ) {
		//char *ddd1 = "";
		//char *ddd2 = "";
		//if ( *s ) ddd1 = " checked";
		//else      ddd2 = " checked";
		// just show the parm name and value if printing in json
		if ( isJSON ) {
			if ( ! lastRow ) {
				long val = 0;
				if ( *s ) val = 1;
				sb->safePrintf("\"%s\":%li,\n",cgi,val);
			}
		}
		else {
			sb->safePrintf("<center><nobr>");
			// this is part of the "HACK" fix below. you have to
			// specify the cgi parm in the POST request, and 
			// unchecked checkboxes are not included in the POST 
			// request.
			//if ( lastRow && m->m_page == PAGE_FILTERS ) 
			//	sb->safePrintf("<input type=hidden ");
			//char *val = "Y";
			//if ( ! *s ) val = "N";
			char *val = "";
			if ( *s ) val = " checked";
			// in case it is not checked, submit that!
			// if it gets checked this should be overridden then
			sb->safePrintf("<input type=hidden name=%s value=0>"
				       , cgi );
			//else
			sb->safePrintf("<input type=checkbox value=1 ");
				       //"<nobr><input type=button ");
			if ( m->m_page == PAGE_FILTERS)
				sb->safePrintf("id=id_%s ",cgi);
			
			sb->safePrintf("name=%s%s"
				       //" onmouseup=\""
				       //"if ( this.value=='N' ) {"
				       //"this.value='Y';"
				       //"} "
				       //"else if ( this.value=='Y' ) {"
				       //"this.value='N';"
				       //"}"
				       //"\" "
				       ">"
				       ,cgi
				       ,val);//,ddd);
			//
			// repeat for off position
			//
			//if ( ! lastRow || m->m_page != PAGE_FILTERS )  {
			//	sb->safePrintf(" Off:<input type=radio ");
			//	if ( m->m_page == PAGE_FILTERS)
			//		sb->safePrintf("id=id_%s ",cgi);
			//	sb->safePrintf("value=0 name=%s%s>",
			//		       cgi,ddd2);
			//}
			sb->safePrintf("</nobr></center>");
		}
	}
	else if ( t == TYPE_CHAR )
		sb->safePrintf ("<input type=text name=%s value=\"%li\" "
				"size=3>",cgi,(long)(*s));
	/*	else if ( t == TYPE_CHAR2 )
		sprintf (p,"<input type=text name=%s value=\"%li\" "
		"size=3>",cgi,*(char*)s);*/
	else if ( t == TYPE_PRIORITY ) 
		printDropDown ( MAX_SPIDER_PRIORITIES , sb , cgi , *s , 
				false , false );
	else if ( t == TYPE_PRIORITY2 ) {
		// just show the parm name and value if printing in json
		if ( isJSON )
			sb->safePrintf("\"%s\":%li,\n",cgi,(long)*(char *)s);
		else
			printDropDown ( MAX_SPIDER_PRIORITIES , sb , cgi , *s ,
					true , true );
	}
	// this url filters parm is an array of SAFEBUFs now, so each is
	// a string and that string is the diffbot api url to use. 
	// the string is empty or zero length to indicate none.
	//else if ( t == TYPE_DIFFBOT_DROPDOWN ) {
	//	char *xx=NULL;*xx=0;
	//}
	else if ( t == TYPE_RETRIES    ) 
		printDropDown ( 4 , sb , cgi , *s , false , false );
	else if ( t == TYPE_PRIORITY_BOXES ) {
		// print ALL the checkboxes when we get the first parm
		if ( j != 0 ) return status;
		printCheckBoxes ( MAX_SPIDER_PRIORITIES , sb , cgi , s );
	}
	else if ( t == TYPE_CMD )
		// if cast=0 it will be executed, otherwise it will be
		// broadcasted with cast=1 to all hosts and they will all
		// execute it
		sb->safePrintf ( "<b><a href=\"/%s?c=%s&%s=1&cast=%li\">"
			  "<center>%s</center></a></b>",
			  g_pages.getPath(m->m_page),coll,
			  cgi,cast,m->m_title);
	else if ( t == TYPE_FLOAT ) {
		// just show the parm name and value if printing in json
		if ( isJSON )
			sb->safePrintf("\"%s\":%f,\n",cgi,*(float *)s);
		else
			sb->safePrintf ("<input type=text name=%s "
					"value=\"%f\" "
					// 3 was ok on firefox but need 6
					// on chrome
					"size=6>",cgi,*(float *)s);
	}
	else if ( t == TYPE_IP ) {
		if ( m->m_max > 0 && j == jend ) 
			sb->safePrintf ("<input type=text name=%s value=\"\" "
					"size=12>",cgi);
		else
			sb->safePrintf ("<input type=text name=%s value=\"%s\" "
					"size=6>",cgi,iptoa(*(long *)s));
	}
	else if ( t == TYPE_LONG ) {
		// just show the parm name and value if printing in json
		if ( isJSON )
			sb->safePrintf("\"%s\":%li,\n",cgi,*(long *)s);
		else
			sb->safePrintf ("<input type=text name=%s "
					"value=\"%li\" "
					// 3 was ok on firefox but need 6
					// on chrome
					"size=6>",cgi,*(long *)s);
	}
	else if ( t == TYPE_LONG_CONST ) 
		sb->safePrintf ("%li",*(long *)s);
	else if ( t == TYPE_LONG_LONG )
		sb->safePrintf ("<input type=text name=%s value=\"%lli\" "
				"size=12>",cgi,*(long long *)s);
	else if ( t == TYPE_STRING || t == TYPE_STRINGNONEMPTY ) {
		long size = m->m_size;
		// give regular expression box on url filters page more room
		//if ( m->m_page == PAGE_FILTERS ) {
		//	if ( size > REGEX_TXT_MAX ) size = REGEX_TXT_MAX;
		//}
		//else {
		if ( size > 20 ) size = 20;
		//}
		sb->safePrintf ("<input type=text name=%s size=%li value=\"",
				cgi,size);
		sb->dequote ( s , gbstrlen(s) );
		sb->safePrintf ("\">");
	}
	// HACK: print a drop down not a textbox for selecting the
	// m_spiderDiffbotApiUrl[]. we can't just store this selection
	// as a number because m_diffbotApiList (a string of comma separated
	// items to select from) can change! it is not a typical dropdown.
	// so we have to record the actual text we selected, which is
	// basically the diffbot api url. this is because john can add
	// custom diffbot api urls at anytime to the list.
	/*
	else if ( t == TYPE_SAFEBUF && strcmp(m->m_cgi,"dapi") == 0 ) {
		SafeBuf *sx = (SafeBuf *)s;
		// just show the parm name and value if printing in json
		if ( isJSON ) {
			// this can be empty for the empty row i guess
			if ( sx->length() ) {
				// convert diffbot # to string
				sb->safePrintf("\"%s\":\"",cgi);
				// this is just the url path, not the title
				// of the menu option... so this would be
				// like "/api/article?u="
				sb->safeUtf8ToJSON (sx->getBufStart() );
				sb->safePrintf("\",\n");
			}
		}
		else
			printDiffbotDropDown ( sb , cgi , THIS , sx );
	}
	*/
	else if ( t == TYPE_SAFEBUF ) {
		long size = m->m_size;
		// give regular expression box on url filters page more room
		if ( m->m_page == PAGE_FILTERS ) {
			//if ( size > REGEX_TXT_MAX ) size = REGEX_TXT_MAX;
			size = 40;
		}
		else {
			if ( size > 20 ) size = 20;
		}
		SafeBuf *sx = (SafeBuf *)s;
		// just show the parm name and value if printing in json
		if ( isJSON ) {
			// this can be empty for the empty row i guess
			if ( sx->length() ) {
				// convert diffbot # to string
				sb->safePrintf("\"%s\":\"",cgi);
				sb->safeUtf8ToJSON (sx->getBufStart() );
				sb->safePrintf("\",\n");
			}
		}
		else {
			sb->safePrintf ("<input type=text name=%s size=%li "
					"value=\"",
					cgi,size);
			//sb->dequote ( s , gbstrlen(s) );
			// note it
			//log("hack: %s",sx->getBufStart());
			sb->dequote ( sx->getBufStart() , sx->length() );
			sb->safePrintf ("\">");
		}
	}
	else if ( t == TYPE_STRINGBOX ) {
		sb->safePrintf("<textarea rows=10 cols=64 name=%s>",cgi);
		//p += urlEncode ( p , pend - p , s , gbstrlen(s) );
		//p += htmlDecode ( p , s , gbstrlen(s) );
		sb->htmlEncode ( s , gbstrlen(s), false );
		//sprintf ( p , "%s" , s );
		//p += gbstrlen(p);		
		sb->safePrintf ("</textarea>\n");
	}
	else if ( t == TYPE_CONSTANT ) 
		sb->safePrintf ("%s",m->m_title);
	else if ( t == TYPE_MONOD2 )
		sb->safePrintf ("%li",j / 2 );
	else if ( t == TYPE_MONOM2 ) {
		/*
		if ( m->m_page == PAGE_PRIORITIES ) {
			if ( j % 2 == 0 ) sb->safePrintf ("old");
			else              sb->safePrintf ("new");
		}
		else
		*/
			sb->safePrintf ("%li",j % 2 );
	}
	else if ( t == TYPE_RULESET ) ;
		// subscript is already included in "cgi"
		//g_pages.printRulesetDropDown ( sb         ,
		//			       user       ,
		//			       cgi        ,
		//			       *(long *)s ,  // selected  
		//			       -1         ); // subscript
	else if ( t == TYPE_TIME ) {
		//time is stored as a string
		//if time is not stored properly, just write 00:00
		if ( s[2] != ':' )
			strncpy ( s, "00:00", 5 );
		char hr[3];
		char min[3];
		memcpy ( hr, s, 2 );
		memcpy ( min, s + 3, 2 );
		hr[2] = '\0';
		min[2] = '\0';
		// print the time in the input forms
		sb->safePrintf("<input type=text name=%shr size=2 "
			       "value=%s>h " 
			       "<input type=text name=%smin size=2 "
			       "value=%s>m " ,
			       cgi    , 
			       hr ,
			       cgi    , 
			       min  );
	}

	else if ( t == TYPE_DATE || t == TYPE_DATE2 ) {
		// time is stored as long
		long ct = *(long *)s;
		// get the time struct
		struct tm *tp = gmtime ( (time_t *)&ct ) ;
		// set the "selected" month for the drop down
		char *ss[12];
		for ( long i = 0 ; i < 12 ; i++ ) ss[i]="";
		long month = tp->tm_mon;
		if ( month < 0 || month > 11 ) month = 0; // Jan
		ss[month] = " selected";
		// print the date in the input forms
		sb->safePrintf(
			"<input type=text name=%sday "
			"size=2 value=%li> "
			"<select name=%smon>"
			"<option value=0%s>Jan"
			"<option value=1%s>Feb"
			"<option value=2%s>Mar"
			"<option value=3%s>Apr"
			"<option value=4%s>May"
			"<option value=5%s>Jun"
			"<option value=6%s>Jul"
			"<option value=7%s>Aug"
			"<option value=8%s>Sep"
			"<option value=9%s>Oct"
			"<option value=10%s>Nov"
			"<option value=11%s>Dec"
			"</select>\n"
			"<input type=text name=%syr size=4 value=%li>"
			"<br>"
			"<input type=text name=%shr size=2 "
			"value=%02li>h " 
			"<input type=text name=%smin size=2 "
			"value=%02li>m " 
			"<input type=text name=%ssec size=2 "
			"value=%02li>s" ,
			cgi    ,
			(long)tp->tm_mday ,
			cgi    ,
			ss[0],ss[1],ss[2],ss[3],ss[4],ss[5],ss[6],ss[7],ss[8],
			ss[9],ss[10],ss[11],
			cgi    ,
			(long)tp->tm_year + 1900 ,
			cgi    , 
			(long)tp->tm_hour ,
			cgi    , 
			(long)tp->tm_min  ,
			cgi    ,
			(long)tp->tm_sec  );
		/*
		if ( t == TYPE_DATE2 ) {
			p += gbstrlen ( p );
			// a long after the long is used for this
			long ct = *(long *)(THIS+m->m_off+4);
			char *ss = "";
			if ( ct ) ss = " checked";
			sprintf ( p , "<br><input type=checkbox "
				  "name=%sct value=1%s> use current "
				  "time\n",cgi,ss);
		}
		*/
	}
	else if ( t == TYPE_SITERULE ) {
		// print the siterec rules as a drop down
		char *ss[5];
		for ( long i = 0; i < 5; i++ ) ss[i] = "";
		long v = *(long*)s;
		if ( v < 0 || v > 4 ) v = 0;
		ss[v] = " selected";
		sb->safePrintf ( "<select name=%s>"
				 "<option value=0%s>Hostname"
				 "<option value=1%s>Path Depth 1"
				 "<option value=2%s>Path Depth 2"
				 "<option value=3%s>Path Depth 3"
				 "</select>\n",
				 cgi, ss[0], ss[1], ss[2], ss[3] );
	}


	// end the input cell
	if ( ! isJSON ) sb->safePrintf ( "</td>\n");

	// "insert above" link? used for arrays only, where order matters
	if ( m->m_addin && j < jend && ! isJSON ) {
		sb->safePrintf ( "<td><a href=\"?c=%s&cast=1&"
				 //"ins_%s=1\">insert</td>\n",coll,cgi );
				 // insert=<rowNum>
				 // "j" is the row #
				 "insert=%li\">insert</td>\n",coll,j );
	}

	// does next guy start a new row?
	bool lastInRow = true; // assume yes
	if ( mm+1<m_numParms&&m->m_rowid>=0&&m_parms[mm+1].m_rowid==m->m_rowid)
		lastInRow = false;
	if ( ((s_count-1) % nc) != (nc-1) ) lastInRow = false;

	// . display the remove link for arrays if we need to
	// . but don't display if next guy does NOT start a new row
	if ( m->m_max > 1 && lastInRow && ! isJSON ) {
	//     m->m_page != PAGE_PRIORITIES ) {
		// show remove link?
		bool show = true;
		if ( j >= jend )  show = false;
		// get # of rows
		long *nr = (long *)((char *)THIS + m->m_off - 4);
		// are we the last row?
		bool lastRow = false;
		// yes, if this is true
		if ( j == *nr - 1 ) lastRow = true;
		// do not allow removal of last default url filters rule
		if ( lastRow && !strcmp(m->m_cgi,"fsp")) show = false;
		char *suffix = "";
		if ( m->m_page == PAGE_SECURITY ) suffix = "ip";
		if ( show )
			sb->safePrintf ("<td><a href=\"?c=%s&cast=1&"
					//"rm_%s=1\">"
					// remove=<rownum>
					"remove%s=%li\">"
					"remove</td>\n",coll,//cgi );
					suffix,
					j); // j is row #
					
		else
			sb->safePrintf ( "<td></td>\n");
	}

	if ( lastInRow && ! isJSON ) sb->safePrintf ("</tr>\n");
	return status;
}

// get the object of our desire
char *Parms::getTHIS ( HttpRequest *r , long page ) {
	// if not master controls, must be a collection rec
	if ( page < PAGE_CGIPARMS ) return (char *)&g_conf;
	char *coll = r->getString ( "c" );
	// support john wanting to use "id" for the crawl id which is really
	// the collection id, hopefully won't conflict with other things.
	if ( ! coll ) coll = r->getString ( "id" );
	if ( ! coll || ! coll[0] )
		//coll = g_conf.m_defaultColl;
		coll = g_conf.getDefaultColl( r->getHost(), r->getHostLen() );
	CollectionRec *cr = g_collectiondb.getRec ( coll );
	if ( ! cr ) log("admin: Collection \"%s\" not found.", 
			r->getString("c") );
	return (char *)cr;
}

/*

//because this can do commands which block, now we pass a callback
//with the request and socket in case they want to block until
//it completes.
bool Parms::setFromRequest ( HttpRequest *r , 
			     //long user ,
			     TcpSocket* s,
			     bool (*callback)(TcpSocket *s , HttpRequest *r),
			     CollectionRec *newcr ) {
	bool retval = true;
	// get the page from the path... like /sockets --> PAGE_SOCKETS
	long page = g_pages.getDynamicPageNumber ( r );
	// is it a collection?
	char *THIS = getTHIS ( r , page );
	// override? THIS will point to default main coll, so override it
	if ( newcr ) THIS = (char *)newcr;
	// ensure valid
	if ( ! THIS ) {
		// it is null when no collection explicitly specified...
		log(LOG_LOGIC,"admin: THIS is null for page %li.",page);
		return retval;
	}
	// . clear all the checkbox parms for this page
	// . if they are unchecked, no cgi parm is provided by the browser!!
	char *action =  r->getString ( "action" );
	if ( action && strcmp(action,"submit" )==0 && 
	     (page == PAGE_SPIDER || page==PAGE_FILTERS) ) {
		// || page == PAGE_PRIORITIES) ) {
		for ( long i = 0 ; i < m_numParms ; i++ ) {
			Parm *m = &m_parms[i];
			if ( m->m_page != page                ) continue;
			if ( m->m_type != TYPE_PRIORITY_BOXES &&
			     m->m_type != TYPE_CHECKBOX         ) continue;
			// clear it
			for ( long j = 0 ; j < m->m_max ; j++ ) // m_fixed
				*(THIS + m->m_off + j) = 0;
		}
	}
	// JAB - invalidate the regex if URL FILTER submit is pressed
	//if ( action && strcmp(action,"submit" )==0 && page == PAGE_FILTERS) {
	//	if ( THIS != (char *)&g_conf )
	//		((CollectionRec*) THIS)->invalidateRegEx ();
	//}

	// reset the sitedb filters table if submitted changes
	//if ( action && strcmp(action,"submit" )==0 && page == PAGE_RULES ) {
	//	if ( THIS != (char *)&g_conf )
	//		((CollectionRec*) THIS)->m_updateSiteRulesTable=1;
	//}

	bool changedUrlFilters = false;

	// loop through cgi parms
	for ( long i = 0 ; i < r->getNumFields() ; i++ ) {
		// get cgi parm name
		char *field = r->getField    ( i );
		long  flen  = r->getFieldLen ( i );
		// get index into array, if it is an array, otherwise, an = -1
		char *d = field + flen ;
		while ( d > field && is_digit ( *(d-1) ) ) d--;
		long an = 0;
		if ( is_digit ( *d ) ) an = atol ( d );
		// ensure we are valid
		if ( an < 0 ) {
			log("admin: Invalid removal of element"
			    "%li in array.", an);
			continue;
		}
		char cc = *d;
		*d = '\0';
		bool insert = false;
		if ( strncmp ( field , "ins_" , 4 ) == 0 ) {
			insert = true;
			field += 4;
		}
		bool remove = false;
		// if it begins with "rm_" it is an array removal request
		if ( strncmp ( field , "rm_" , 3 ) == 0 ) {
			remove = true;
			field +=3 ;
		}
		// find in parms list
		long  j;
		Parm *m;
		for ( j = 0 ; j < m_numParms ; j++ ) {
			// get it
			m = &m_parms[j];
			// . skip if offset is negative, that means none
			// . no, could be a command
			//if ( m->m_off < 0 ) continue;
			// skip if no cgi parm, may not be configurable now
			if ( ! m->m_cgi ) continue;
			if ( m->m_type == TYPE_TIME ){
				char *cgi = m->m_cgi;
				long  len = gbstrlen(cgi);
				if (strncmp(field,cgi,len)) continue;
				// if not the hour skip it
				if (flen != len + 2 ) continue;
				if (field[flen-2] != 'h' ) continue;
				if (field[flen-1] != 'r' ) continue;
				// we got a match
			}
				
			// . compare up to first parm's cgi field name
			// . date parms append letters to their base for 
			//   the year,month,day,hour,minute
			else if ( m->m_type==TYPE_DATE || 
				  m->m_type==TYPE_DATE2 ) {
				char *cgi = m->m_cgi;
				long  len = gbstrlen(cgi);
				if (strncmp(field,cgi,len)) continue;
				// if not the year skip it
				if (flen != len + 2 ) continue;
				if (field[flen-2] != 'y' ) continue;
				if (field[flen-1] != 'r' ) continue;
				// we got a match
			}
			// otherwise, must match the cgi name exactly
			else if ( strcmp ( field,m->m_cgi ) != 0 ) continue;
			// make sure we got the right parms for what we want
			if (THIS == (char *)&g_conf && m->m_obj != OBJ_CONF ) 
				continue;
			if (THIS != (char *)&g_conf && m->m_obj == OBJ_CONF ) 
				continue;
			// got it
			break;
		}
		// restore the field name in the cgi part of the url
		*d = cc;
		// bail if the cgi field is not in the parms list
		if ( j >= m_numParms ) continue;
		// parm "m" must be from the same page as the page we are on
		// UNLESS parm is PAGE_NONE. that way CommandPowerNotice()
		// will work.
		bool onPage = true;
		if ( m->m_page != page && m->m_page != PAGE_NONE ) 
			onPage = false;
		// if showing the url filters page on the crawlbot/diffbot page
		// then allow this to go through!
		if ( ! onPage && 
		     m->m_page == PAGE_FILTERS &&
		     page      == PAGE_CRAWLBOT )
			onPage = true;
		// if parm is not on the page we are viewing, skip it!
		if ( ! onPage ) continue;
		// if they provided a url filters parm, then assume they
		// are changing the url filters and reset waiting tree
		if ( m->m_page == PAGE_FILTERS ) changedUrlFilters = true;
		// insert row above it if we should (only applicable to 
		// non-fixed arrays)
		if ( insert && m->m_max > 1 ) {
			// get everyone in his row
			long a = j;
			long b = j;
			long rowid = m_parms[j].m_rowid;
			while ( rowid>=0 && a-1>=0 && 
				m_parms[a-1].m_rowid==rowid ) a--;
			while ( rowid>=0 && b+1<m_numParms && 
				m_parms[b+1].m_rowid==rowid ) b++;
			for ( long k = a ; k <= b ; k++ )
				insertParm ( k , an , THIS );
			// need to save it
			if ( THIS != (char *)&g_conf ) 
				((CollectionRec *)THIS)->m_needsSave = true;
			continue;			
		}
		// remove it if we should (only applicable to non-fixed arrays)
		if ( remove && m->m_max > 1 ) {
			// get everyone in his row
			long a = j;
			long b = j;
			long rowid = m_parms[j].m_rowid;
			while ( rowid>=0 && a-1>=0 && 
				m_parms[a-1].m_rowid==rowid ) a--;
			while ( rowid>=0 && b+1<m_numParms && 
				m_parms[b+1].m_rowid==rowid ) b++;
			for ( long k = a ; k <= b ; k++ )
				removeParm ( k , an , THIS );
			// need to save it
			if ( THIS != (char *)&g_conf ) 
				((CollectionRec *)THIS)->m_needsSave = true;
			continue;
		}
		// value of cgi parm
		char *v;
		// used to build a proper date or time from various cgi vars
		char ddd[64];
		if ( m->m_type == TYPE_TIME ) {
			char *cgi = m->m_cgi;
			// set the value
			char cgihr  [10];
			char cgimin [10];
			sprintf ( cgihr  , "%shr"  , cgi );
			sprintf ( cgimin , "%smin" , cgi );
			long hr  = r->getLong  (cgihr  , 0    ) ;
			long min = r->getLong  (cgimin , 0    ) ;

			if ( hr < 0 || hr > 23 ) hr = 0;
			if ( min < 0 || min > 59 ) min = 0;
			sprintf ( ddd , "%02li:%02li", hr, min );
			v = ddd;
		}

		// if we matched a date parm, set the value special
		else if ( m->m_type == TYPE_DATE || m->m_type == TYPE_DATE2 ) {
			char *cgi = m->m_cgi;
			// set the value
			char cgiyr  [10];
			char cgimon [10];
			char cgiday [10];
			char cgihr  [10];
			char cgimin [10];
			char cgisec [10];
			sprintf ( cgiyr  , "%syr"  , cgi );
			sprintf ( cgimon , "%smon" , cgi );
			sprintf ( cgiday , "%sday" , cgi );
			sprintf ( cgihr  , "%shr"  , cgi );
			sprintf ( cgimin , "%smin" , cgi );
			sprintf ( cgisec , "%ssec" , cgi );
			static char *mnames[] = {
				"Jan","Feb","Mar","Apr","May","Jun",
				"Jul","Aug","Sep","Oct","Nov","Dec"};
			long mm = r->getLong(cgimon , 0);
			if ( mm < 0 || mm > 11 ) mm = 0;
			sprintf ( ddd , "%li %s %li %li:%li:%li",
				  r->getLong  (cgiday , 0    ) ,
				  mnames[mm],
				  r->getLong  (cgiyr  , 2004 ) ,
				  r->getLong  (cgihr  , 0    ) ,
				  r->getLong  (cgimin , 0    ) ,
				  r->getLong  (cgisec , 0    ) );
			v = ddd;
		}
		// get the new value (null terminated)
		else v = r->getValue ( i );
		// . skip if no value was provided
		// . unless it was a string! so we can make them empty.
		if ( v[0] == '\0' && 
		     m->m_type != TYPE_STRING && 
		     m->m_type != TYPE_STRINGBOX ) continue;
		// if a command, do it
		if ( m->m_type == TYPE_CMD ) {
			if(! m->m_func (s, r, callback) ) {
				//sanity check
				if(!retval) {
					//this means that we are trying to 
					//do two commands which block, who
					//calls the callback?
					log(LOG_LOGIC,"admin: two blocking"
					    "commands issued at the same"
					    "time.");
					char *xx = NULL; *xx = 0;
				}
				retval = false;
			}
			continue;
		}
		// skip if offset is negative, that means none
		if ( m->m_off < 0 ) continue;
		// skip if no permission
		//if ( (m->m_perms & user) == 0 ) continue;
		//if (m->m_type == TYPE_PRIORITY_BOXES)
		//	log("PRIORITY BOX");
		// set it
		setParm ( (char *)THIS , m, j, an, v, false,//not html enc
			  true );
		// need to save it
		if ( THIS != (char *)&g_conf ) 
			((CollectionRec *)THIS)->m_needsSave = true;
		// . ensure our array element count is at least that
		// . do we have an array? skip if not.
		//if ( m->m_max <= 0 ) continue;
		// . is this element we're adding bumping up the count?
		// . array count is 4 bytes before the array
		//char *pos =  (char *)THIS + m->m_off - 4 ;
		// set the count to it if it is bigger than current count
		//if ( an + 1 > *(long *)pos ) *(long *)pos = an + 1; mdw
	}

	// have to reset and recompute "waitingtree" if url filters change
	if ( changedUrlFilters && THIS != (char *)&g_conf ) {
		// cast it
		CollectionRec *cr = (CollectionRec *)THIS;
		// to prevent us having to rebuild doledb/waitingtree at startup
		// we need to make the spidercoll here so it is not null
		SpiderColl *sc = g_spiderCache.getSpiderColl(cr->m_collnum);
		// get it
		//SpiderColl *sc = cr->m_spiderColl;
		// this will rebuild the waiting tree
		if ( sc ) sc->urlFiltersChanged();
	}

	// so g_spiderCache can reload if sameDomainWait, etc. have changed
	g_collectiondb.updateTime();
	return retval;
}
*/

bool Parms::insertParm ( long i , long an ,  char *THIS ) {
	Parm *m = &m_parms[i];
	// . shift everyone above down
	// . first long at offset is always the count
	//   for arrays
	char *pos =  (char *)THIS + m->m_off ;
	long  num = *(long *)(pos - 4);
	// ensure we are valid
	if ( an >= num || an < 0 ) {
		log("admin: Invalid insertion of element "
		    "%li in array of size %li for \"%s\".",
		    an,num,m->m_title);
		return false;
	}
	// also ensure that we have space to put the parm in, because in
	// case of URl filters, it is bounded by MAX_FILTERS
	if ( num >= MAX_FILTERS ){
		log("admin: Invalid insert of element %li, array is full "
		    "in size %li for \"%s\".",an, num, m->m_title);
		return false;
	}
	// point to the place where the element is to be inserted
	char *src = pos + m->m_size * an;

	//point to where it is to be moved
	char *dst = pos + m->m_size * ( an + 1 );

	// how much to move
	long size = ( num - an ) * m->m_size ;
	// move them
	memmove ( dst , src , size );
	// if the src was a TYPE_SAFEBUF clear it so we don't end up doing
	// a double free, etc.!
	memset ( src , 0 , m->m_size );
	// inc the count
	*(long *)(pos-4) = (*(long *)(pos-4)) + 1;
	// put the defaults in the inserted line
	setParm ( (char *)THIS , m , i , an , m->m_def , false ,false );
	return true;
}

bool Parms::removeParm ( long i , long an , char *THIS ) {
	Parm *m = &m_parms[i];
	// . shift everyone above down
	// . first long at offset is always the count
	//   for arrays
	char *pos =  (char *)THIS + m->m_off ;
	long  num = *(long *)(pos - 4);
	// ensure we are valid
	if ( an >= num || an < 0 ) {
		log("admin: Invalid removal of element "
		    "%li in array of size %li for \"%s\".",
		    an,num,m->m_title);
		return false;
	}
	// point to the element being removed
	char *dst = pos + m->m_size * an;
	// free memory pointed to by safebuf, if we are safebuf, before
	// overwriting it... prevents a memory leak
	if ( m->m_type == TYPE_SAFEBUF ) {
		SafeBuf *dx = (SafeBuf *)dst;
		dx->purge();
	}
	// then point to the good stuf
	char *src = pos + m->m_size * (an+1);
	// how much to bury it with
	long size = (num - an - 1 ) * m->m_size ;
	// bury it
	memcpy ( dst , src , size );
	// dec the count
	*(long *)(pos-4) = (*(long *)(pos-4)) - 1;
	return true;
}

void Parms::setParm ( char *THIS , Parm *m , long mm , long j , char *s ,
		      bool isHtmlEncoded , bool fromRequest ) {
	// . this is just for setting CollectionRecs, so skip if offset < 0
	// . some parms are just for SearchInput (search parms)
	if ( m->m_off < 0 ) return;

	float oldVal = 0;
	float newVal = 0;

	if ( ! s ) {
		s = "0";
		char *tit = m->m_title;
		if ( ! tit || ! tit[0] ) tit = m->m_xml;
		log(LOG_LOGIC,"admin: Parm \"%s\" had NULL default value.",
		    tit);
		//char *xx = NULL; *xx = 0;
	}

	// sanity check
	if ( &m_parms[mm] != m ) {
		log(LOG_LOGIC,"admin: Not sane parameters.");
		char *xx = NULL; *xx = 0;
	}

	// if attempting to add beyond array max, bail out
	if ( j >= m->m_max && j >= m->m_fixed ) {
		log ( "admin: Attempted to set parm beyond limit. Aborting." );
		return;
	}

	// if we are setting a guy in an array AND he is NOT the first
	// in his row, ensure the guy before has a count of j+1 or more.
	//
	// crap, on the url filters page if you do not check "spidering 
	// enabled" checkbox when adding a new rule at the bottom of the
	// table, , then the spidering enabled parameter does not transmit so
	// the "respider frequency" ends up checking the "spider enabled"
	// array whose "count" was not incremented like it should have been.
	// HACK: make new line at bottom always have spidering enabled
	// checkbox set and make it impossible to unset.
	/*
	if ( m->m_max > 1 && m->m_rowid >= 0 && mm > 0 &&
	     m_parms[mm-1].m_rowid == m->m_rowid ) {
		char *pos =  (char *)THIS + m_parms[mm-1].m_off - 4 ;
		long maxcount = *(long *)pos;
		if ( j >= maxcount ) {
			log("admin: parm before \"%s\" is limiting us",
			    m_parms[mm-1].m_title);
			//log("admin: try nuking the url filters or whatever "
			//    "and re-adding");
			return;
		}
	}
	*/

	// ensure array count at least j+1
	if ( m->m_max > 1 ) {
		// . is this element we're adding bumping up the count?
		// . array count is 4 bytes before the array
		char *pos =  (char *)THIS + m->m_off - 4 ;
		// set the count to it if it is bigger than current count
		if ( j + 1 > *(long *)pos ) *(long *)pos = j + 1;
	}

	char  t   = m->m_type;
	if      ( t == TYPE_CHAR           || 
		  t == TYPE_CHAR2          || 
		  t == TYPE_CHECKBOX       ||
		  t == TYPE_BOOL           ||
		  t == TYPE_BOOL2          ||
		  t == TYPE_PRIORITY       || 
		  t == TYPE_PRIORITY2      || 
		  //t == TYPE_DIFFBOT_DROPDOWN ||
		  t == TYPE_PRIORITY_BOXES || 
		  t == TYPE_RETRIES        ||
		  t == TYPE_FILTER           ) {
		if ( fromRequest && *(char *)(THIS + m->m_off + j) == atol(s))
			return;
		if ( fromRequest)oldVal = (float)*(char *)(THIS + m->m_off +j);
		*(char *)(THIS + m->m_off + j) = atol ( s );
 		newVal = (float)*(char *)(THIS + m->m_off + j);
		goto changed; }
	else if ( t == TYPE_CMD ) {
		log(LOG_LOGIC, "conf: Parms: TYPE_CMD is not a cgi var.");
		return;	}
	else if ( t == TYPE_DATE2 || t == TYPE_DATE ) {
		long v = (long)atotime ( s ); 
		if ( fromRequest && *(long *)(THIS + m->m_off + 4*j) == v ) 
			return;
		*(long *)(THIS + m->m_off + 4*j) = v;
		if ( v < 0 ) log("conf: Date for <%s> of \""
				 "%s\" is not in proper format like: "
				 "01 Jan 1980 22:45",m->m_xml,s);
		goto changed; }
	else if ( t == TYPE_FLOAT ) {
		if( fromRequest &&
		    *(float *)(THIS + m->m_off + 4*j) == (float)atof ( s ) ) 
			return;
		// if changed within .00001 that is ok too, do not count
		// as changed, the atof() has roundoff errors
		//float curVal = *(float *)(THIS + m->m_off + 4*j);
		//float newVal = atof(s);
		//if ( newVal < curVal && newVal + .000001 >= curVal ) return;
		//if ( newVal > curVal && newVal - .000001 <= curVal ) return;
		if ( fromRequest ) oldVal = *(float *)(THIS + m->m_off + 4*j);
		*(float *)(THIS + m->m_off + 4*j) = (float)atof ( s );
		newVal = *(float *)(THIS + m->m_off + 4*j);
		goto changed; }
	else if ( t == TYPE_IP ) {
		if ( fromRequest && *(long *)(THIS + m->m_off + 4*j) == 
		     (long)atoip (s,gbstrlen(s) ) ) 
			return;
		*(long *)(THIS + m->m_off + 4*j) = (long)atoip (s,gbstrlen(s) ); 
		goto changed; }
	else if ( t == TYPE_LONG || t == TYPE_LONG_CONST || t == TYPE_RULESET||
		  t == TYPE_SITERULE ) {
		long v = atol ( s );
		// min is considered valid if >= 0
		if ( m->m_min >= 0 && v < m->m_min ) v = m->m_min;
		if ( fromRequest && *(long *)(THIS + m->m_off + 4*j) == v ) 
			return;
		if ( fromRequest)oldVal=(float)*(long *)(THIS + m->m_off +4*j);
		*(long *)(THIS + m->m_off + 4*j) = v;
		newVal = (float)*(long *)(THIS + m->m_off + 4*j);
		goto changed; }
	else if ( t == TYPE_LONG_LONG ) {
		if ( fromRequest &&
		     *(unsigned long long *)(THIS + m->m_off+8*j)==
		     strtoull(s,NULL,10))
			return;
		*(long long *)(THIS + m->m_off + 8*j) = strtoull(s,NULL,10);
		goto changed; }
	// like TYPE_STRING but dynamically allocates
	else if ( t == TYPE_SAFEBUF ) {
		long len = gbstrlen(s);
		// no need to truncate since safebuf is dynamic
		//if ( len >= m->m_size ) len = m->m_size - 1; // truncate!!
		//char *dst = THIS + m->m_off + m->m_size*j ;
		// point to the safebuf, in the case of an array of
		// SafeBufs "j" is the # in the array, starting at 0
		SafeBuf *sb = (SafeBuf *)(THIS+m->m_off+(j*sizeof(SafeBuf)) );
		long oldLen = sb->length();
		// why was this commented out??? we need it now that we
		// send email alerts when parms change!
		if ( fromRequest &&
		     ! isHtmlEncoded && oldLen == len &&
		     memcmp ( sb->getBufStart() , s , len ) == 0 ) 
			return;
		// nuke it
		sb->purge();
		// this means that we can not use string POINTERS as parms!!
		if ( ! isHtmlEncoded ) sb->safeMemcpy ( s , len ); 
		else                   len = sb->htmlDecode (s,len,false,0);
		// ensure null terminated
		sb->nullTerm();
		// note it
		//log("hack: %s",s);

		// null term it all
		//dst[len] = '\0';
		//sb->reserve ( 1 );
		// null terminate but do not include as m_length so the
		// memcmp() above still works right
		//sb->m_buf[sb->m_length] = '\0';
		// . might have to set length
		// . used for CollectionRec::m_htmlHeadLen and m_htmlTailLen
		//if ( m->m_plen >= 0 ) 
		//	*(long *)(THIS + m->m_plen) = len ;
		goto changed; 
	}
	else if ( t == TYPE_STRING         || 
		  t == TYPE_STRINGBOX      || 
		  t == TYPE_STRINGNONEMPTY ||
		  t == TYPE_TIME            ) {
		long len = gbstrlen(s);
		if ( len >= m->m_size ) len = m->m_size - 1; // truncate!!
		char *dst = THIS + m->m_off + m->m_size*j ;
		// why was this commented out??? we need it now that we
		// send email alerts when parms change!
		if ( fromRequest &&
		     ! isHtmlEncoded && (long)gbstrlen(dst) == len &&
		     memcmp ( dst , s , len ) == 0 ) 
			return;
		// this means that we can not use string POINTERS as parms!!
		if ( ! isHtmlEncoded ) memcpy ( dst , s , len ); 
		else                   len = htmlDecode (dst , s,len,false,0);
		dst[len] = '\0';
		// . might have to set length
		// . used for CollectionRec::m_htmlHeadLen and m_htmlTailLen
		if ( m->m_plen >= 0 ) 
			*(long *)(THIS + m->m_plen) = len ;
		goto changed; }
 changed:
	// tell gigablast the value is EXPLICITLY given -- no longer based
	// on default.conf
	//if ( m->m_obj == OBJ_COLL ) ((CollectionRec *)THIS)->m_orig[mm] = 2;

	// we do not recognize timezones corectly when this is serialized
	// into coll.conf, it says UTC, which is ignored in HttpMime.cpp's
	// atotime() function. and when we submit it i think we use the
	// local time zone, so the values end up changing every time we 
	// submit!!! i think it might read it in as UTC then write it out
	// as local time, or vice versa.
	if ( t == TYPE_DATE || t == TYPE_DATE2 ) return;

	// do not send if setting from startup
	if ( ! fromRequest ) return;

	// note it in the log
	log("admin: parm \"%s\" changed value",m->m_title);

	long long nowms = gettimeofdayInMillisecondsLocal();

	// . note it in statsdb
	// . record what parm change and from/to what value
	g_statsdb.addStat ( 0, // niceness ,
			    "parm_change" ,
			    nowms,
			    nowms,
			    0         , // value
			    m->m_hash , // parmHash
			    oldVal,
			    newVal);

	// only send email alerts if we are host 0 since everyone syncs up
	// with host #0 anyway
	if ( g_hostdb.m_hostId != 0 ) return;

	// send an email alert notifying the admins that this parm was changed
	// BUT ALWAYS send it if email alerts were just TURNED OFF 
	// ("sea" = Send Email Alerts)
	if ( ! g_conf.m_sendEmailAlerts && strcmp(m->m_cgi,"sea") != 0 )
		return;

	// if spiders we turned on, do not send an email alert, cuz we
	// turn them on when we restart the cluster
	if ( strcmp(m->m_cgi,"se")==0 && g_conf.m_spideringEnabled )
		return;

	char tmp[1024];
	Host *h0 = g_hostdb.getHost ( 0 );
	long ip0 = 0;
	if ( h0 ) ip0 = h0->m_ip;
	sprintf(tmp,"%s: parm \"%s\" changed value",iptoa(ip0),m->m_title);
	g_pingServer.sendEmail ( NULL  , // Host ptr
				 tmp   , // msg
				 true  , // sendToAdmin
				 false , // oom?
				 false , // kernel error?
				 true  , // parm change?
				 true  );// force it? even if disabled?

	// now the spider collection can just check the collection rec
	//long long nowms = gettimeofdayInMilliseconds();
	//((CollectionRec *)THIS)->m_lastUpdateTime = nowms;

	return;
}

Parm *Parms::getParmFromParmHash ( long parmHash ) {
	for ( long i = 0 ; i < m_numParms ; i++ ) {
		Parm *m = &m_parms[i];
		if ( m->m_hash != parmHash ) continue;
		return m;
	}
	return NULL;
}


void Parms::setToDefault ( char *THIS ) {
	// init if we should
	init();

	// . clear out any coll rec to get the diffbotApiNum dropdowns
	// . this is a backwards-compatibility hack since this new parm
	//   will not be in old coll.conf files and will not be properly
	//   initialize when displaying a url filter row.
	//if ( THIS != (char *)&g_conf ) {
	//	CollectionRec *cr = (CollectionRec *)THIS;
	//	memset ( cr->m_spiderDiffbotApiNum , 0 , MAX_FILTERS);
	//}

	for ( long i = 0 ; i < m_numParms ; i++ ) {
		Parm *m = &m_parms[i];
		if ( m->m_type == TYPE_COMMENT ) continue;
		if ( m->m_type == TYPE_MONOD2  ) continue;
		if ( m->m_type == TYPE_MONOM2  ) continue;
		if ( m->m_type == TYPE_CMD     ) continue;
		if (THIS == (char *)&g_conf && m->m_obj != OBJ_CONF ) continue;
		if (THIS != (char *)&g_conf && m->m_obj == OBJ_CONF ) continue;
		// sanity check, make sure it does not overflow
		if ( m->m_obj != OBJ_CONF && 
		     m->m_off > (long)sizeof(CollectionRec)){
			log(LOG_LOGIC,"admin: Parm in Parms.cpp should use "
			    "OBJ_COLL not OBJ_CONF");
			char *xx = NULL; *xx = 0;
		}
		//if ( m->m_page == PAGE_PRIORITIES )
		//	log("hey");
		// or 
		if ( m->m_page > PAGE_CGIPARMS &&
		     m->m_page != PAGE_NONE &&
		     m->m_obj == OBJ_CONF ) {
			log(LOG_LOGIC,"admin: Page can not reference "
			    "g_conf and be declared AFTER PAGE_CGIPARMS in "
			    "Pages.h. Title=%s",m->m_title);
			char *xx = NULL; *xx = 0;
		}
		// leave arrays empty, set everything else to default
		if ( m->m_max <= 1 ) {
			//if ( i == 282 )  // "query" parm
			//	log("hey");
			setParm ( THIS , m, i, 0, m->m_def, false/*not enc.*/,
				  false );
			//((CollectionRec *)THIS)->m_orig[i] = 1;
			//m->m_orig = 0; // set in setToDefaults()
		}
		// these are special, fixed size arrays
		if ( m->m_fixed > 0 ) {
			for ( long k = 0 ; k < m->m_fixed ; k++ ) {
				setParm(THIS,m,i,k,m->m_def,false/*not enc.*/,
					false);
				//m->m_orig = 0; // set in setToDefaults()
				//((CollectionRec *)THIS)->m_orig[i] = 1;
			}
			continue;
		}
		// make array sizes 0
		if ( m->m_max <= 1 ) continue;
		// otherwise, array is not fixed size
		char *s = THIS + m->m_off ;
		// set count to 1 if a default is present
		//if (   m->m_def[0] ) *(long *)(s-4) = 1;
		//else                 *(long *)(s-4) = 0;
		*(long *)(s-4) = 0;
	}
}

// . returns false and sets g_errno on error
// . you should set your "THIS" to its defaults before calling this
bool Parms::setFromFile ( void *THIS        , 
			  char *filename    , 
			  char *filenameDef ) {
	// make sure we're init'd
	init();
	// let em know
	if ( THIS == &g_conf ) log (LOG_INIT,"conf: Reading %s." , filename );
	// . let the log know what we are doing
	// . filename is NULL if a call from CollectionRec::setToDefaults()
	Xml xml;
	char buf [ MAX_XML_CONF ];
	if ( filename && ! setXmlFromFile (&xml,filename,buf,MAX_XML_CONF) )
		return false;

	// . all the collectionRecs have the same default file in
	//   the workingDir/collections/default.conf
	// . so use our built in buffer for that
	/*
	if ( THIS != &g_conf && ! m_isDefaultLoaded ) {
		m_isDefaultLoaded = true;
		File f;
		f.set ( filenameDef );
		if ( ! f.doesExist() ) {
			log(LOG_INIT,
			    "db: Default collection configuration file "
			    "%s was not found. Newly created collections "
			    "will use hard coded defaults.",f.getFilename());
			goto skip;
		}
		if ( ! setXmlFromFile ( &m_xml2     , 
					filenameDef ,
					m_buf       ,
					MAX_XML_CONF ) ) return false;
	}

 skip:
	*/
	long  vlen;
	char *v ;
	//char  c ;
	long numNodes  = xml.getNumNodes();
	long numNodes2 = m_xml2.getNumNodes();

	// now set THIS based on the parameters in the xml file
	for ( long i = 0 ; i < m_numParms ; i++ ) {
		// get it
		Parm *m = &m_parms[i];
		//log(LOG_DEBUG, "Parms: %s: parm: %s", filename, m->m_xml);
		// . there are 2 object types, coll recs and g_conf, aka
		//   OBJ_COLL and OBJ_CONF.
		// . make sure we got the right parms for what we want
		if ( THIS == &g_conf && m->m_obj != OBJ_CONF ) continue;
		if ( THIS != &g_conf && m->m_obj == OBJ_CONF ) continue;
		// skip comments and command
		if ( m->m_type == TYPE_COMMENT  ) continue;
		if ( m->m_type == TYPE_MONOD2   ) continue;
		if ( m->m_type == TYPE_MONOM2   ) continue;
		if ( m->m_type == TYPE_CMD      ) continue;
		if ( m->m_type == TYPE_CONSTANT ) continue;
		// these are special commands really
		if ( m->m_type == TYPE_BOOL2    ) continue;
		//if ( strcmp ( m->m_xml , "users" ) == 0 )
		//	log("got it");
		// we did not get one from first xml file yet
		bool first = true;
		// array count
		long j = 0;
		// node number
		long nn = 0;
		// a tmp thingy
		char tt[1];
		long nb;
		long newnn;
	loop:
		// get xml node number of m->m_xml in the "xml" file
		newnn = xml.getNodeNum(nn,1000000,m->m_xml,gbstrlen(m->m_xml));
#ifdef _GLOBALSPEC_
		if ( m->m_priv == 2 ) continue;
		if ( m->m_priv == 3 ) continue;
#elif _CLIENT_
		// always use default value if client not allowed control of
		if ( m->m_priv ) continue;
#elif _METALINCS_
		if ( m->m_priv == 2 ) continue;
		if ( m->m_priv == 3 ) continue;
#endif

		// debug
		//log("%s --> %li",m->m_xml,nn);
		// try default xml file if none, but only if first try
		if ( newnn < 0 && first ) goto try2; 
		// it is valid, use it
		nn = newnn;
		// set the flag, we've committed the array to the first file
		first = false;
		// otherwise, we had some in this file, but now we're out
		if ( nn < 0 ) continue;
		// . next node is the value of this tag
		// . skip if none there
		if ( nn + 1 >= numNodes ) continue;
		// point to it
		v    = xml.getNode    ( nn + 1 );
		vlen = xml.getNodeLen ( nn + 1 );
		// if a back tag... set the value to the empty string
		if ( v[0] == '<' && v[1] == '/' ) vlen = 0;
		// now, extricate from the <![CDATA[ ... ]]> tag if we need to
		if ( m->m_type == TYPE_STRING         || 
		     m->m_type == TYPE_STRINGBOX      ||
		     m->m_type == TYPE_SAFEBUF        ||
		     m->m_type == TYPE_STRINGNONEMPTY   ) {
			char *oldv    = v;
			long  oldvlen = vlen;
			// if next guy is NOT a tag node, try the next one
			if ( v[0] != '<' && nn + 2 < numNodes ) {
				v    = xml.getNode    ( nn + 2 );
				vlen = xml.getNodeLen ( nn + 2 );
			}
			// should be a <![CDATA[...]]>
			if ( vlen<12 || strncasecmp(v,"<![CDATA[",9)!=0 ) {
				log("conf: No <![CDATA[...]]> tag found "
				    "for \"<%s>\" tag. Trying without CDATA.",
				    m->m_xml);
				v    = oldv;
				vlen = oldvlen;
			}
			// point to the nugget
			else {
				v    += 9;
				vlen -= 12;
			}
		}
		// get the value
		//v = xml.getString ( nn , nn+2 , m->m_xml , &vlen );
		// this only happens when tag is there, but without a value
		if ( ! v || vlen == 0 ) { vlen = 0; v = tt; }
		//c = v[vlen];
		v[vlen]='\0';
		if ( vlen == 0 ){ 
			// . this is generally ok
			// . this is spamming the log so i am commenting out! (MDW)
			//log(LOG_INFO, "parms: %s: Empty value.", m->m_xml);
			// Allow an empty string
			//continue;
		}

		// now decode it into itself
		nb = htmlDecode ( v , v , vlen , false ,0);
		v[nb] = '\0';
		// set our parm
		setParm ( (char *)THIS, m, i, j, v, false/*is html encoded?*/,
			  false );
		// we were set from the explicit file
		//((CollectionRec *)THIS)->m_orig[i] = 2;
		// go back
		//v[vlen] = c;
		// do not repeat same node
		nn++;
		// try to get the next node if we're an array
		if ( ++j < m->m_max || j < m->m_fixed ) { goto loop; }
		// otherwise, if not an array, go to next parm
		continue;
	try2:
		// get xml node number of m->m_xml in the "m_xml" file
		nn = m_xml2.getNodeNum(nn,1000000,m->m_xml,gbstrlen(m->m_xml));
		// otherwise, we had one in file, but now we're out
		if ( nn < 0 ) {
			// if it was ONLY a search input parm, with no
			// default value that can be changed in the 
			// CollectionRec then skip it
			if ( m->m_soff  != -1 && 
			     m->m_off   == -1 &&
			     m->m_smaxc == -1 ) 
				continue;
			// . if it is a string, like <adminIp> and default is 
			//   NULL then don't worry about reporting it
			// . no, just make the default "" then
			//if ( m->m_type==TYPE_STRING && ! m->m_def) continue;
			// bitch that it was not found
			//if ( ! m->m_def[0] ) 
			//	log("conf: %s does not have <%s> tag. "
			//	    "Ommitting.",filename,m->m_xml);
			//else 
			if ( ! m->m_def ) //m->m_def[0] )
				log("conf: %s does not have <%s> tag. Using "
				    "default value of \"%s\".", filename,
				    m->m_xml,m->m_def);
			continue;
		}
		// . next node is the value of this tag
		// . skip if none there
		if ( nn + 1 >= numNodes2 ) continue;
		// point to it
		v    = m_xml2.getNode    ( nn + 1 );
		vlen = m_xml2.getNodeLen ( nn + 1 );
		// if a back tag... set the value to the empty string
		if ( v[0] == '<' && v[1] == '/' ) vlen = 0;
		// now, extricate from the <![CDATA[ ... ]]> tag if we need to
		if ( m->m_type == TYPE_STRING         || 
		     m->m_type == TYPE_STRINGBOX      ||
		     m->m_type == TYPE_STRINGNONEMPTY   ) {
			char *oldv    = v;
			long  oldvlen = vlen;
			// reset if not a tag node
			if ( v[0] != '<' && nn + 2 < numNodes2 ) {
				v    = m_xml2.getNode    ( nn + 2 );
				vlen = m_xml2.getNodeLen ( nn + 2 );
			}
			// should be a <![CDATA[...]]>
			if ( vlen<12 || strncasecmp(v,"<![CDATA[",9)!=0 ) {
				log("conf: No <![CDATA[...]]> tag found "
				    "for \"<%s>\" tag. Trying without CDATA.",
				    m->m_xml);
				v    = oldv;
				vlen = oldvlen;
			}
			// point to the nugget
			else {
				v    += 9;
				vlen -= 12;
			}
		}
		// get the value
		//v = m_xml2.getString ( nn , nn+2 , m->m_xml , &vlen );
		// this only happens when tag is there, but without a value
		if ( ! v || vlen == 0 ) { vlen = 0; v = tt; }
		//c = v[vlen];
		v[vlen]='\0';
		// now decode it into itself
		nb = htmlDecode ( v , v , vlen , false,0);
		v[nb] = '\0';
		// set our parm
		setParm ( (char *)THIS, m, i, j, v, false/*is html encoded?*/,
			  false );
		// we were set from the backup default file
		//((CollectionRec *)THIS)->m_orig[i] = 1;
		// go back
		//v[vlen] = c;
		// do not repeat same node
		nn++;
		// try to get the next node if we're an array
		if ( ++j < m->m_max || j < m->m_fixed ) { goto loop; }
		// otherwise, if not an array, go to next parm
		continue;
	}

	// always make sure we got some admin security
	if ( g_conf.m_numMasterIps <= 0 && g_conf.m_numMasterPwds <= 0 ) {
		//log(LOG_INFO,
		//    "conf: No master IP or password provided. Using default "
		//    "password 'footbar23'." );
		//g_conf.m_masterIps[0] = atoip ( "64.139.94.202", 13 );
		//g_conf.m_numMasterIps = 1;
		strcpy ( g_conf.m_masterPwds[0] , "footbar23" );
		g_conf.m_numMasterPwds = 1;
	}

	return true;
}

// returns false and sets g_errno on error
bool Parms::setXmlFromFile(Xml *xml, char *filename, char *buf, long bufSize){
	File f;
	f.set ( filename );
	// is it too big?
	long fsize = f.getFileSize();
	if ( fsize > bufSize ) {
		log ("conf: File size of %s is %li, must be "
		     "less than %li.",f.getFilename(),fsize,bufSize );
		char *xx = NULL; *xx = 0;
	}
	// open it for reading
	f.set ( filename );
	if ( ! f.open ( O_RDONLY ) )
		return log("conf: Could not open %s: %s.",
		    filename,mstrerror(g_errno));
	// read in the file
	long numRead = f.read ( buf , bufSize , 0 /*offset*/ );
	f.close ( );
	if ( numRead != fsize )
	      return log ("conf: Could not read %s : %s.",
			  filename,mstrerror(g_errno));
	// null terminate it
	buf [ fsize ] = '\0';
	// . remove all comments in case they contain tags
	// . if you have a # as part of your string, it must be html encoded,
	//   just like you encode < and >
	char *s = buf;
	char *d = buf;
	while ( *s ) {
		// . skip comments
		// . watch out for html encoded pound signs though
		if ( *s == '#' ) {
			if (s>buf && *(s-1)=='&' && is_digit(*(s+1))) goto ok;
			while ( *s && *s != '\n' ) s++; 
			continue; 
		}
		// otherwise, transcribe over
	ok:
		*d++ = *s++;
	}
	*d = '\0';
	bufSize = d - buf;
	// . set to xml
	// . use version of 0
	return xml->set ( buf     , 
			  bufSize , 
			  false   , // ownData
			  0       , // allocSize
			  false   , // pureXml?
			  0       , // version
			  true    , // setParents
			  0       , // niceness
			  CT_XML  );
}

#define MAX_CONF_SIZE 200000

// returns false and sets g_errno on error
bool Parms::saveToXml ( char *THIS , char *f ) {
	if ( g_conf.m_readOnlyMode ) return true;
	// print into buffer
	char  buf[MAX_CONF_SIZE];
	char *p    = buf;
	char *pend = buf + MAX_CONF_SIZE;
	long  len ;
	long  n   ;
	File  ff  ;
	long  j   ;
	long  count;
	char *s;
	CollectionRec *cr = NULL;
	if ( THIS != (char *)&g_conf ) cr = (CollectionRec *)THIS;
	// now set THIS based on the parameters in the xml file
	for ( long i = 0 ; i < m_numParms ; i++ ) {
		// get it
		Parm *m = &m_parms[i];
		// . there are 2 object types, coll recs and g_conf, aka
		//   OBJ_COLL and OBJ_CONF.
		// . make sure we got the right parms for what we want
		if ( THIS == (char *)&g_conf && m->m_obj != OBJ_CONF) continue;
		if ( THIS != (char *)&g_conf && m->m_obj == OBJ_CONF) continue;
		if ( m->m_type == TYPE_MONOD2  ) continue;
		if ( m->m_type == TYPE_MONOM2  ) continue;
		if ( m->m_type == TYPE_CMD ) continue;
		if ( m->m_type == TYPE_BOOL2 ) continue;
		// ignore if hidden as well! no, have to keep those separate
		// since spiderroundnum/starttime is hidden but should be saved
		if ( m->m_flags & PF_NOSAVE ) continue;
		// ignore if diffbot and we are not a diffbot/custom crawl
		if ( cr && 
		     ! cr->m_isCustomCrawl &&
		     (m->m_flags & PF_DIFFBOT) ) continue;
		// skip if we should not save to xml
		if ( ! m->m_save ) continue;
		// allow comments though
		if ( m->m_type == TYPE_COMMENT ) goto skip2;
		// skip if this was compiled for a client and they should not
		// see this control
#ifdef _GLOBALSPEC_
		if ( m->m_priv == 2 ) continue;
		if ( m->m_priv == 3 ) continue;
#elif _CLIENT_
		if ( m->m_priv ) continue;
#elif _METALINCS_
		if ( m->m_priv == 2 ) continue;
		if ( m->m_priv == 3 ) continue;
#endif
		// skip if offset is negative, that means none
		if ( m->m_off < 0 ) continue;
		s = (char *)THIS + m->m_off ;
		// if array, count can be 0 or more than 1
		count = 1;
		if ( m->m_max   > 1 ) count = *(long *)(s-4);
		if ( m->m_fixed > 0 ) count = m->m_fixed;
		// sanity check
		if ( count > 100000 ) {
			log(LOG_LOGIC,"admin: Outrageous array size in for "
			    "parameter %s. Does the array max size long "
			    "preceed it in the conf class?",m->m_title);
			exit(-1);
		}
skip2:
		// description, do not wrap words around lines
		char *d = m->m_desc;
		// if empty array mod description to include the tag name
		char tmp [10*1024];
		if ( m->m_max > 1 && count == 0 && gbstrlen(d) < 9000 &&
		     m->m_xml && m->m_xml[0] ) {
			char *cc = "";
			if ( d && d[0] ) cc = "\n";
			sprintf ( tmp , "%s%sUse <%s> tag.",d,cc,m->m_xml);
			d = tmp;
		}
		char *END  = d + gbstrlen(d);
		char *dend;
		char *last;
		char *start;
		// just print tag if it has no description
		if ( ! *d ) goto skip;
		if ( p + gbstrlen(d)+5 >= pend ) goto hadError;
		if ( p > buf ) *p++='\n';
	loop:
		dend  = d + 77;
		if ( dend > END ) dend = END;
		last  = d;
		start = d;
		while ( *d && d < dend ) { 
			if ( *d == ' '  ) last = d; 
			if ( *d == '\n' ) { last = d; break; }
			d++; 
		}
		if ( ! *d ) last = d;
		memcpy ( p , "# " , 2 ); 
		p += 2;
		memcpy ( p , start , last - start );
		p += last - start;
		*p++='\n';
		d = last + 1;
		if ( d < END && *d ) goto loop;
		// bail if comment
		if ( m->m_type == TYPE_COMMENT ) {
			//sprintf ( p , "\n" );
			//p += gbstrlen ( p );
			continue;
		}
		if ( m->m_type == TYPE_MONOD2  ) continue;
		if ( m->m_type == TYPE_MONOM2  ) continue;

	skip:
		/* . note: this code commented out because it was specific to
		     an old client
		// if value is from default collection file, do not
		// explicitly list it
		if ( m->m_obj == OBJ_COLL &&
		     ((CollectionRec *)THIS)->m_orig[i] == 1 ) {
			sprintf ( p ,"# Value for <%s> tag taken from "
				  "default.conf.\n",m->m_xml );
			p += gbstrlen ( p );
			continue;
		}
		*/

		// debug point
		//if ( m->m_type == TYPE_SAFEBUF )
		//	log("hey");

		// loop over all in this potential array
		for ( j = 0 ; j < count ; j++ ) {
			// the xml
			if ( p + gbstrlen(m->m_xml) >= pend ) goto hadError;
			sprintf ( p , "<%s>" , m->m_xml );
			p += gbstrlen ( p );
			// print CDATA if string
			if ( m->m_type == TYPE_STRING         || 
			     m->m_type == TYPE_STRINGBOX      ||
			     m->m_type == TYPE_SAFEBUF        ||
			     m->m_type == TYPE_STRINGNONEMPTY   ) {
				sprintf ( p , "<![CDATA[" );
				p += gbstrlen ( p );
			}
			// break point
			//if (strcmp ( m->m_xml , "filterRulesetDefault")==0)
			//	log("got it");
			// . represent it in ascii form
			// . this escapes out <'s and >'s
			// . this ALSO encodes #'s (xml comment indicators)
			p = getParmHtmlEncoded(p,pend,m,s);
			// print CDATA if string
			if ( m->m_type == TYPE_STRING         || 
			     m->m_type == TYPE_STRINGBOX      ||
			     m->m_type == TYPE_SAFEBUF        ||
			     m->m_type == TYPE_STRINGNONEMPTY   ) {
				sprintf ( p , "]]>" );
				p += gbstrlen ( p );
			}
			// this is NULL if it ran out of room
			if ( ! p ) goto hadError;
			// advance to next element in array, if it is one
			s = s + m->m_size;
			// close the xml tag
			if ( p + 4 >= pend ) goto hadError;
			sprintf ( p , "</>\n" );
			p += gbstrlen ( p );
		}
	}
	*p = '\0';

	ff.set ( f );
	if ( ! ff.open ( O_RDWR | O_CREAT | O_TRUNC ) )
		return log("db: Could not open %s : %s",
			   ff.getFilename(),mstrerror(g_errno));

	// save the parm to the file
	len = gbstrlen(buf);
	// use -1 for offset so we do not use pwrite() so it will not leave
	// garbage at end of file
	n    = ff.write ( buf , len , -1 );
	ff.close();
	if ( n == len ) return true;
	return log("admin: Could not write to file %s.",ff.getFilename());
 hadError:
	return log("admin: File bigger than %li bytes."
		   "  Please increase #define in Parms.cpp.",
		   (long)MAX_CONF_SIZE);
}

Parm *Parms::getParm ( char *cgi ) {
	for ( long i = 0 ; i < m_numParms ; i++ ) {
		if ( ! m_parms[i].m_cgi ) continue ;
		if (   m_parms[i].m_cgi[0] != cgi[0] ) continue;
		if (   m_parms[i].m_cgi[1] != cgi[1] ) continue;
		if (   strcmp ( m_parms[i].m_cgi , cgi ) == 0 ) 
			return &m_parms[i];
	}
	return NULL;
}

/*
Parm *Parms::getParm2 ( char *cgi , long cgiLen ) {
	for ( long i = 0 ; i < m_numParms ; i++ ) {
		if ( ! m_parms[i].m_cgi ) continue ;
		if (   m_parms[i].m_cgi[0] != cgi[0] ) continue;
		if (   cgiLen >=2 && m_parms[i].m_cgi[1] != cgi[1] ) continue;
		// only compare as many letters as the cgi name has
		if (   strncmp ( m_parms[i].m_cgi , cgi , cgiLen ) ) continue;
		// that means we gotta check lengths next
		if ( gbstrlen(m_parms[i].m_cgi) != cgiLen ) continue;
		// got a match
		return &m_parms[i];
	}
	return NULL;
}
*/
/*
#define PHTABLE_SIZE (MAX_PARMS*2)

Parm *Parms::getParm ( char *cgi ) {
	// make the hash table for the first call
	static long  s_phtable [ PHTABLE_SIZE ];
	static Parm *s_phparm  [ PHTABLE_SIZE ];
	static bool  s_init = false;
	// do not re-make the table if we already did
	if ( s_init ) goto skipMakeTable;
	// ok, now make the table
	s_init = true;
	memset ( s_phparm , 0 , PHTABLE_SIZE );
	for ( long i = 0 ; i < m_numParms ; i++ ) {
		if ( ! m_parms[i].m_cgi ) continue ;
		long h = hash32 ( m_parms[i].m_cgi );
		long n = h % PHTABLE_SIZE;
	while ( s_phparm[n] ) {
		// . sanity check
			// . we don't have that many parms, they should never 
			//   collide!!... but it is possible i guess.
			if ( s_phtable[n] == h ) {
				log(LOG_LOGIC,"Parms: collisions forbidden in "
				    "getParm(). Duplicate cgi name?");
				char *xx = NULL; *xx = 0;
			}
			if (++n >= PHTABLE_SIZE) n = 0;
		}
		s_phtable[n] = h; // fill the bucket
		s_phparm [n] = m; // the parm
	}
skipMakeTable:
	// look up in table
	long h = hash32 ( cgi );
	long n = h % PHTABLE_SIZE;
	// while bucket is occupied and does not equal our hash... chain
	while ( s_phparm[n] && s_phtable[n] != h ) 
		if (++n >= PHTABLE_SIZE) n = 0;
	// if empty, no match
	return s_phparm[n];
}
*/

char *Parms::getParmHtmlEncoded ( char *p , char *pend , Parm *m , char *s ) {
	// do not breech the buffer
	if ( p + 100 >= pend ) return p;
	// print it out
	char t = m->m_type;
	if ( t == TYPE_CHAR           || t == TYPE_BOOL           ||
	     t == TYPE_CHECKBOX       ||
	     t == TYPE_PRIORITY       || t == TYPE_PRIORITY2      || 
	     //t == TYPE_DIFFBOT_DROPDOWN ||
	     t == TYPE_PRIORITY_BOXES || t == TYPE_RETRIES        ||
	     t == TYPE_RETRIES        || t == TYPE_FILTER         ||
	     t == TYPE_BOOL2          || t == TYPE_CHAR2           ) 
		sprintf (p,"%li",(long)*s);
	else if ( t == TYPE_FLOAT )
		sprintf (p,"%f",*(float *)s);
	else if ( t == TYPE_IP ) 
		sprintf (p,"%s",iptoa(*(long *)s));
	else if ( t == TYPE_LONG || t == TYPE_LONG_CONST || t == TYPE_RULESET||
		  t == TYPE_SITERULE ) 
		sprintf (p,"%li",*(long *)s);
	else if ( t == TYPE_LONG_LONG )
		sprintf (p,"%lli",*(long long *)s);
	else if ( t == TYPE_SAFEBUF ) {
		SafeBuf *sb = (SafeBuf *)s;
		char *buf = sb->getBufStart();
		long blen = 0;
		if ( buf ) blen = gbstrlen(buf);
		p = htmlEncode ( p , pend , buf , buf + blen , true ); // #?*
	}
	else if ( t == TYPE_STRING         || 
		  t == TYPE_STRINGBOX      ||
		  t == TYPE_STRINGNONEMPTY ||
		  t == TYPE_TIME) {
		long slen = gbstrlen ( s );
		// this returns the length of what was written, it may
		// not have converted everything if pend-p was too small...
		//p += saftenTags2 ( p , pend - p , s , len );
		p = htmlEncode ( p , pend , s , s + slen , true /*#?*/);
	}
	else if ( t == TYPE_DATE || t == TYPE_DATE2 ) {
		// time is stored as long
		long ct = *(long *)s;
		// get the time struct
		struct tm *tp = localtime ( (time_t *)&ct ) ;
		// set the "selected" month for the drop down
		strftime ( p , 100 , "%d %b %Y %H:%M UTC" , tp );
	}
	p += gbstrlen ( p );
	return p;
}
/*
// returns the size needed to serialize parms
long Parms::getStoredSize() {
	long size = 0;

	// calling serialize with no ptr gets size
	serialize( NULL, &size );
	return size;
}

// . serialize parms to buffer
// . accepts addr of buffer ptr and addr of buffer size
// . on entry buf can be NULL to determine required size
// . if buf is not NULL, *bufSize must specify the size of buf
// . on exit *buf is filled with serialized parms
// . on exit *bufSize is set to the actual len of *buf 
bool Parms::serialize( char *buf, long *bufSize ) {
	g_errno = 0;
 	if ( ! bufSize ) {
		g_errno = EBADENGINEER;
		log( "admin: serialize: bad engineer: no bufSize ptr" );
		*bufSize = 0;
		return false;
	}
	bool sizeChk = false;
	char *end = NULL;
	if ( ! buf ) sizeChk = true; // just calc size
	else end = buf + *bufSize;   // for overrun checking

	// serialize  OBJ_CONF and OBJ_COLL parms 
	*bufSize = 0;
	char *p = buf;

	// now the parms
	struct SerParm *sp = NULL;
	for ( long i = 0 ; i < m_numParms ; i++ ) {
		Parm *m = &m_parms[i];
		
		// ignore these:
		if ( m->m_obj == OBJ_SI ) continue;    
		if ( m->m_off < 0 ) continue;          
		if ( m->m_type == TYPE_COMMENT ) continue;
		if ( m->m_type == TYPE_MONOD2  ) continue;
		if ( m->m_type == TYPE_MONOM2  ) continue;
		if ( m->m_type == TYPE_CMD     ) continue;
		if ( m->m_type == TYPE_LONG_CONST ) continue;
		if ( ! m->m_sync ) continue;  // parm is not to be synced

		// determine the size of the parm value
		long size = 0;
		if ( m->m_type == TYPE_CHAR           ) size = 1;
		if ( m->m_type == TYPE_CHAR2          ) size = 1;
		if ( m->m_type == TYPE_CHECKBOX       ) size = 1;
		if ( m->m_type == TYPE_BOOL           ) size = 1;
		if ( m->m_type == TYPE_BOOL2          ) size = 1;
		if ( m->m_type == TYPE_PRIORITY       ) size = 1;
		if ( m->m_type == TYPE_PRIORITY2      ) size = 1;
		//if ( m->m_type == TYPE_DIFFBOT_DROPDOWN) size = 1;
		if ( m->m_type == TYPE_PRIORITY_BOXES ) size = 1;
		if ( m->m_type == TYPE_RETRIES        ) size = 1;
		if ( m->m_type == TYPE_TIME           ) size = 6;
		if ( m->m_type == TYPE_DATE2          ) size = 4;
		if ( m->m_type == TYPE_DATE           ) size = 4;
		if ( m->m_type == TYPE_FLOAT          ) size = 4;
		if ( m->m_type == TYPE_IP             ) size = 4;
		if ( m->m_type == TYPE_RULESET        ) size = 4;
		if ( m->m_type == TYPE_LONG           ) size = 4;
		if ( m->m_type == TYPE_LONG_LONG      ) size = 8;
		if ( m->m_type == TYPE_STRING         ) size = m->m_size;
		if ( m->m_type == TYPE_STRINGBOX      ) size = m->m_size;
		if ( m->m_type == TYPE_STRINGNONEMPTY ) size = m->m_size;
		if ( m->m_type == TYPE_SAFEBUF        ) size = m->m_size;
		if ( m->m_type == TYPE_SITERULE       ) size = 4;

		// . set size to the total size of array
		// . set cnt to the number of itmes
		long cnt = 1;
		if (m->m_fixed > 0) {
			size *= m->m_fixed;
			cnt = m->m_fixed;
		}
		else {
			size *= m->m_max;
			cnt = m->m_max;
		}

		if ( m->m_obj == OBJ_CONF ) {
			bool overflew = serializeConfParm( m, i, &p, end, 
							   size, cnt, 
							   sizeChk, bufSize );
			if ( overflew ) goto overflow;
		}
	       	else if ( m->m_obj == OBJ_COLL )  {
			collnum_t j = g_collectiondb.getFirstCollnum ();
			while ( j >= 0 ) {
				CollectionRec *cr = g_collectiondb.getRec( j );
				bool overflew = serializeCollParm( cr,
								   m, i, &p, 
								   end,
								   size, cnt,
								   sizeChk,
								   bufSize );
				if ( overflew ) goto overflow;
				j = g_collectiondb.getNextCollnum ( j );
			}
		} 
	}
	if ( ! sizeChk ) {
		// set the final marker to 0s to indicate the end
		sp = (struct SerParm *)p;
		sp->i = 0;
		sp->obj = 0;
		sp->size = 0;
		sp->cnt = 0;
	}
	*bufSize += sizeof( struct SerParm );

	return true;
 
 overflow:
	g_errno = EBADENGINEER;
	log(LOG_WARN, "admin: serialize: bad engineer: overflow" );
	*bufSize = 0;
	return false;
}

// . serialize a conf parm
// . if sizeChk is true then we do not serialize, but just get the
//   bytes required if we did serialize
// . serialize parm into *p, the cursor i guess, buf end is "end"
bool Parms::serializeConfParm( Parm *m, long i, char **p, char *end,
			       long size, long cnt, 
			       bool sizeChk, long *bufSz ) {
	SerParm *sp = NULL;

	// safebuf not supported here yet, but it for coll recs below
	// so copy code from there if you need it
	if ( m->m_type == TYPE_SAFEBUF ) { char *xx=NULL;*xx=0;}

	if (m->m_type == TYPE_STRING || 
	    m->m_type == TYPE_STRINGBOX || 
	    m->m_type == TYPE_STRINGNONEMPTY ) {
		char *sVal = NULL;
		if ( ! sizeChk ) {
			sp = (SerParm *)*p;
			sp->i = i;          // index of parm
			sp->obj = OBJ_CONF; 
			sp->size = 0L;      // 0 for strings
			sp->cnt = cnt;      // # of strings
			// if an array, get num of member
			if ( cnt > 1 ) {
				sp->off = m->m_off - sizeof(long);
				sp->num = *(long *)((char *)&g_conf 
					 + sp->off);
			}
			else {
				sp->off = 0;
				sp->num = 0;
			}

			sVal = sp->val;
		}
		char *sConf = (char *)&g_conf + m->m_off;
		long totLen = 0;
		long tcnt = cnt;
		while ( tcnt ) {
			long len = gbstrlen( sConf );
			if ( ! sizeChk ) {
				// copy the parm value
				if ( sVal + len > end ) 
					return true; // overflow
				strcpy( sVal, sConf );
			}
			totLen += len + 1; // incl the NULL
			// inc conf ptr by size of strings
			sConf += m->m_size;
			// inc ser value by len of str + NULL
			sVal += len + 1;
			tcnt--;
		}
		if ( ! sizeChk ) {
			// inc by tot len of compacted strings
			*p += sizeof( *sp ) + totLen;
		} 
		*bufSz += sizeof( SerParm ) + totLen;
	}
	else {
		if ( ! sizeChk ) {
			sp = (SerParm *)*p;
			sp->i = i;
			sp->obj = OBJ_CONF; 
			sp->size = size;   // tot size if array
			sp->cnt = cnt;     // num of items
			// if array, get num of member
			if ( cnt > 1 ) {
				sp->off = m->m_off - sizeof(long);
				sp->num = *(long *)((char *)&g_conf 
					 + sp->off);
			}
			else {
				sp->off = 0;
				sp->num = 0;
			}
			
			// copy the parm's whole value
			if ( sp->val + size > end )
				return true; // overflow
			memcpy( sp->val, 
				(char *)&g_conf + m->m_off, size );
			// inc by tot size if array
			*p += sizeof( *sp ) + size; 
		}
		*bufSz += sizeof( SerParm ) + size;
	}

	return false;
}

// . serialize a coll parm in CollectionRec.h
// . if sizeChk is true then we do not serialize, but just get the
//   bytes required if we did serialize
// . serialize parm into *p, the cursor i guess, buf end is "end"
bool Parms::serializeCollParm( CollectionRec *cr,
			       Parm *m, long i, char **p, char *end,
			       long size, long cnt,
			       bool sizeChk, long *bufSize) {
	SerParm *sp = NULL;

	if (m->m_type == TYPE_STRING || 
	    m->m_type == TYPE_STRINGBOX || 
	    m->m_type == TYPE_SAFEBUF ||
	    m->m_type == TYPE_STRINGNONEMPTY ) {
		char *sVal = NULL;
		if ( ! sizeChk ) {
			sp = (SerParm *)*p;
			sp->i = i;     // index of parm
			sp->obj = OBJ_COLL; 
			sp->size = 0L; // 0 for strings
			sp->cnt = cnt; // # of strings
			// is this parm an array if parms?
			if ( cnt > 1 ) {
				// the offset of the "count" or the 
				// "number of elements" in the array.
				// it preceeds the value of the first element
				// as can be seen infor parms in 
				// CollectionRec.h.
				sp->off = m->m_off - sizeof(long);
				// store the # of then into "num"
				sp->num = *(long *)((char *)cr + sp->off);
			}
			else {
				sp->off = 0;
				sp->num = 0;
			}
			sVal = sp->val;
		}
		// point to the actual parm itself
		char *sColl = (char *)cr + m->m_off;
		long totLen = 0;
		// "cnt" is how many elements in the array
		long tcnt = cnt;
		while ( tcnt ) {
			// the  length of the string
			long len;
			// the string
			char *pstr;
			// if a safebuf, point to string it has
			if ( m->m_type == TYPE_SAFEBUF ) {
				SafeBuf *sx = (SafeBuf *)sColl;
				pstr = sx->getBuf();
				len = sx->length();
				if ( ! pstr ) pstr = "";
			}
			// get length of the string. if not a safebuf it will
			// just be an outright string in CollectionRec.h
			else {
				pstr = sColl;
				len = gbstrlen( sColl );
			}
			if ( ! sizeChk ) {
				// copy the string
				if  ( sVal+len > end ) {
					log("parms: buffer too small");
					return true; 
				}
				// this puts a \0 at the end
				strcpy( sVal, pstr );
			}
			totLen += len + 1; // incl NULL
			// . inc cr ptr by size of strs
			// . this is the size of the SafeBuf for TYPE_SAFEBUF
			sColl += m->m_size;
			// . inc the write cursor by string length + the \0
			sVal += len + 1;
			tcnt--;
		}
		if ( ! sizeChk ) {
			// inc by tot len of cmpctd str
			*p += sizeof( *sp ) + totLen; 
		}
		*bufSize += sizeof( SerParm ) + totLen;
	}
	else {
		if ( ! sizeChk ) {
			sp = (SerParm *)*p;
			sp->i = i;
			sp->obj = OBJ_COLL;
			sp->size = size; // tot size
			sp->cnt = cnt;  // num of items
			// get num of member
			if ( cnt > 1 ) {
				sp->off = m->m_off - sizeof(long);
				sp->num = *(long *)((char *)cr + sp->off);
			}
			else {
				sp->off = 0;
				sp->num = 0;
			}
			// copy whole value
			if ( sp->val + size > end )
				return true;
			memcpy( sp->val, 
				(char *)cr + m->m_off, 
				size );
			// inc by whole size of value
			*p += sizeof( *sp ) + size; 
		}
		*bufSize += sizeof( SerParm ) + size;
	}

	return false;
}


// deserialize parms from buffer and set our values to the new values
void Parms::deserialize( char *buf ) {
	g_errno = 0;
	char *p = buf;
	bool confChgd = false;

	SerParm *sp = (SerParm *)p;
	long numLooped = 0;
	const long MAX_LOOP = (long)(MAX_PARMS*1.5);
	// if one of these is non-zero, we're still working
	while ( (sp->obj || sp->size || sp->cnt) && 
		(sp->obj > 0 && sp->size > 0 && sp->cnt > 0) &&
		numLooped < MAX_LOOP ) {
		// grab the parm we're working on
		if ( sp->i < 0 || sp->i >= m_numParms ) {
			log( "admin: invalid parm # in Parms::deserialize" );
			char *xx = NULL; *xx = 0;
		}
		Parm *m = &m_parms[ sp->i ]; 
		
		if ( sp->obj == OBJ_CONF ) {
			deserializeConfParm( m, sp, &p, &confChgd );
			sp = (struct SerParm *)p;
		}
		else if ( sp->obj == OBJ_COLL ) {
			collnum_t j = g_collectiondb.getFirstCollnum ();
			//if(j <= 0) { 
                        //      log("coll: Collectiondb does not have a rec" );
                        //        return;
                        //}
                        while ( j >= 0 ) {
				CollectionRec *cr = g_collectiondb.getRec( j );
				deserializeCollParm( cr, 
						     m, sp, &p );
				sp = (SerParm *)p;
				j = g_collectiondb.getNextCollnum ( j );

			}
		}

		// setup the next rec
		sp = (SerParm *)p;
		numLooped++;
	}
	if (numLooped >= MAX_LOOP) {
		log( "admin: infinite loop in Parms::deserialize(). halting!");
		char *xx = NULL; *xx = 0;
	}

	// if we changed the conf, we need to save it
	if ( confChgd ) {
		g_conf.save ();
	}

	// if we changed a CollectionRec, we need to save it
	long j = g_collectiondb.getFirstCollnum ();
	while ( j >= 0 ) {
		CollectionRec *cr = g_collectiondb.getRec( j );
		if ( cr->m_needsSave ) {
			cr->save ();
			// so g_spiderCache can reload if sameDomainWait, etc.
			// have changed
			g_collectiondb.updateTime();
		}
		j = g_collectiondb.getNextCollnum ( j );
	}
}

void Parms::deserializeConfParm( Parm *m, SerParm *sp, char **p,
				 bool *confChgd ) {
	if ( m->m_off + sp->size > (long)sizeof(g_conf) || 
	     m->m_off + sp->size < 0 ){
		log(LOG_WARN, "admin: deserializing parm would overflow "
		    "the collection rec!");
		char *xx =0; *xx = 0;
	}
	if ( sp->size == 0 ) { // string
		char *sVal = sp->val;
		char *sConf = (char *)&g_conf + m->m_off;
		long totLen = 0;
		bool goodParm = true;
		long tcnt = sp->cnt;
		while ( tcnt ) {
			goodParm = (goodParm && 0 == strcmp( sVal, sConf ));
			long len = gbstrlen( sVal );
			totLen += len + 1;
			// inc ser value by len of str + NULL
			sVal += len + 1;
			// inc conf ptr by size of strings
			sConf += m->m_size;
			tcnt--;
		}
		if ( goodParm ) {
			// . inc by sizeof rec and tot len of compacted array
			*p += sizeof( *sp ) + totLen;
			return;
		}
		// parms don't match
		sVal = sp->val;
		sConf = (char *)&g_conf + m->m_off;
		totLen = 0;
		tcnt = sp->cnt;
		while ( tcnt ) {
			// copy an array value to this parm
			strcpy( sConf, sVal );
			long len = gbstrlen( sVal );
			totLen += len + 1; // incl the NULL
			// inc conf ptr by size of strings
			sConf += m->m_size;
			// inc ser value by len of str + NULL
			sVal += len + 1;
			tcnt--;
		}
		
		// set num of member
		if ( sp->off ) {
			long *tmp = (long *)((char *)&g_conf + sp->off);
			*tmp = sp->num;
		}
		
		// log the changed parm
		log( LOG_INFO, "admin: Parm "
		     "#%li \"%s\" (\"%s\") in conf "
		     "changed on sync.", 
		     sp->i, m->m_cgi, m->m_title );
		
		*confChgd = true;
		
		// inc by sizeof rec and tot len of compacted array
		*p += sizeof( *sp ) + totLen;
	}
	else {
		bool goodParm = ( 0 == memcmp( sp->val, 
					       (char *)&g_conf + m->m_off, 
					       sp->size ) );
		if ( ! goodParm ) {
			// copy the new parm to m's loc
			memcpy( (char *)&g_conf + m->m_off, sp->val, 
				sp->size );

			// set num of member
			if ( sp->off ) {             
				long *tmp = (long *)((char *)&g_conf 
						     + sp->off);
				*tmp = sp->num;
			}
			
			// log the changed parm
			log( LOG_INFO, "admin: Parm "
			     "#%li \"%s\" (\"%s\") in conf "
			     "changed on sync.", 
			     sp->i, m->m_cgi, m->m_title );
			
			*confChgd = true;
		}
		// increase by rec size and size of parm
		*p += sizeof( *sp ) + sp->size;
	}
}

void Parms::deserializeCollParm( CollectionRec *cr,
				 Parm *m, SerParm *sp, char **p ) {
	if ( m->m_off + sp->size > (long)sizeof(CollectionRec) || 
	     m->m_off + sp->size < 0 ) {
		log(LOG_WARN, "admin: deserializing parm would overflow "
		    "the collection rec!");
		char *xx =0; *xx = 0;
	}
	if ( sp->size == 0 ) { // strings
		char *sVal = sp->val; // the sent string buffer i guess
		char *sColl = (char *)cr + m->m_off; // what we have
		long totLen = 0;
		long tcnt = sp->cnt; // # of strings
		bool goodParm = true;
		while ( tcnt ) {

			char *pstr;
			if ( m->m_type == TYPE_SAFEBUF ) {
				SafeBuf *sx = (SafeBuf *)sColl;
				pstr = sx->getBuf();
			}
			else {
				pstr = sColl;
			}

			// set goodParm to true if unchanged
			goodParm= (goodParm && 0 == strcmp(sVal, pstr));
			// get length of what was sent to us
			long len = gbstrlen( sVal );
			totLen += len + 1; //incl NULL
			// this is a list of strings with \0s (sent to us)
			sVal += len + 1;   //incl NULL
			// inc by size of strs. point to next string we have
			// stored in our array of strings in CollectionRec.
			// for TYPE_SAFEBUF this size is sizeof(SafeBuf).
			sColl += m->m_size;
			tcnt--;
		}
		// if parm was an exact match return now
		if ( goodParm ) {
			// . inc by sizeof rec and 
			//   tot len of compacted array
			// . skip the SerParm and following string buffer.
			*p += sizeof( *sp ) + totLen;
			return;
		}
		//
		// if parms don't match, we need to update our stuff
		//
		//
		// point to the sent string buffer
		sVal = sp->val;
		// point to the local parm, array of strings or safebufs
		sColl = (char *)cr + m->m_off;
		totLen = 0;
		// how many strings or safebufs in there?
		tcnt = sp->cnt;
		// loop over each one
		while ( tcnt ) {
			if ( m->m_type == TYPE_SAFEBUF ) {
				SafeBuf *sx = (SafeBuf *)sColl;
				sx->set ( sVal );
				sx->nullTerm ( );
			}
			else {
				// copy an array value to this parm
				strcpy( sColl, sVal );
			}
			// get length of string we copied
			long len = gbstrlen( sVal );
			totLen += len + 1; // +the NULL
			// . inc conf ptr by size 
			//   of strings
			sColl += m->m_size;
			// . inc ser value by len of str + NULL
			sVal += len + 1;
			tcnt--;
		}
		// we changed the record
		cr->m_needsSave = true;
		
		// set num of member
		if ( sp->off ) {
			long *tmp = (long *)((char *)cr + sp->off);
			*tmp = sp->num;
		}
		
		// log the changed parm
		log( LOG_INFO, "admin: Parm "
		     "#%li \"%s\" (\"%s\") in "
		     "collection \"%s\" "
		     "changed on sync.", 
		     sp->i, m->m_cgi, m->m_title,
		     cr->m_coll );
		
		// . inc by sizeof rec and 
		//   tot len of compacted array
		*p += sizeof( *sp ) + totLen;
	}
	else {
		// sanity
		if ( m->m_type == TYPE_SAFEBUF ) { char *xx=NULL;*xx=0; }

		if ( 0 != memcmp( sp->val, (char *)cr + m->m_off, sp->size) ) {
			// copy the new value
			memcpy( (char *)cr + m->m_off, 
				sp->val,
				sp->size );
			
			// set num of member
			if ( sp->off ) {
				long *tmp = (long *)((char *)cr + sp->off);
				*tmp = sp->num;
			}
			
			// log the changed parm
			log( LOG_INFO, "admin: Parm "
			     "#%li \"%s\" (\"%s\") "
			     "in collection \"%s\" "
			     "changed on sync.", 
			     sp->i, m->m_cgi, 
			     m->m_title,
			     cr->m_coll );
			
			// we changed the record
			cr->m_needsSave = true;
		}
		// inc by rec size and tot len of array
		*p += sizeof( *sp ) + sp->size;
	}
}
*/

void Parms::init ( ) {
	// initialize the Parms class if we need to, only do it once
	static bool s_init = false ;
	if ( s_init ) return;
	s_init = true ;

	// default all
	for ( long i = 0 ; i < MAX_PARMS ; i++ ) {
		m_parms[i].m_parmNum= i;
		m_parms[i].m_hash   = 0         ;
		m_parms[i].m_title  = ""         ; // for detecting if not set
		m_parms[i].m_desc   = ""         ; // for detecting if not set
		m_parms[i].m_cgi    = NULL       ; // for detecting if not set
		m_parms[i].m_off    = -1         ; // for detecting if not set
		m_parms[i].m_def    = NULL       ; // for detecting if not set
		m_parms[i].m_type   = TYPE_NONE  ; // for detecting if not set
		m_parms[i].m_page   = -1         ; // for detecting if not set
		m_parms[i].m_obj    = -1         ; // for detecting if not set
		m_parms[i].m_max    =  1         ; // max elements in array
		m_parms[i].m_fixed  =  0         ; // size of fixed size array
		m_parms[i].m_size   =  0         ; // max string size
		m_parms[i].m_cast   =  1 ; // send to all hosts?
		m_parms[i].m_rowid  = -1 ; // rowid of -1 means not in row
		m_parms[i].m_addin  =  0 ; // add insert row command?
		m_parms[i].m_rdonly =  0 ; // is command off in read-only mode?
		m_parms[i].m_hdrs   =  1 ; // assume to always print headers
		m_parms[i].m_perms  =  0 ; // same as containing WebPages perms
		m_parms[i].m_plen   = -1 ; // offset for strings length
		m_parms[i].m_group  =  1 ; // start of a new group of controls?
		m_parms[i].m_priv   =  0 ; // is it private?
		m_parms[i].m_save   =  1 ; // save to xml file?
		m_parms[i].m_min    = -1 ; // min value (for long parms)
		// search fields
		m_parms[i].m_sparm  = 0;
		m_parms[i].m_scmd   = "/search";
		m_parms[i].m_scgi   = NULL;// defaults to m_cgi
		m_parms[i].m_flags  = 0;
		m_parms[i].m_icon   = NULL;
		m_parms[i].m_class  = NULL;
		m_parms[i].m_qterm  = NULL;
		m_parms[i].m_subMenu= 0;
		m_parms[i].m_spriv  = 0;
		//         m_sdefo  = -1;   // just use m_off for this!
		m_parms[i].m_sminc  = -1;  // min in collection rec
		m_parms[i].m_smaxc  = -1;  // max in collection rec
		m_parms[i].m_smin   = 0x80000000; // 0xffffffff;
		m_parms[i].m_smax   = 0x7fffffff;
		m_parms[i].m_soff   = -1; // offset into SearchInput
		m_parms[i].m_sprpg  =  1; // propagate to other pages via GET
		m_parms[i].m_sprpp  =  1; // propagate to other pages via POST
		m_parms[i].m_sync   = true;
	}

	// inherit perms from page
	//for ( long i = 1 ; i < MAX_PARMS ; i++ ) 
	//	if ( m_parms[i].m_page )
	//		m_parms[i].m_perms = m_parms[i-1].m_perms;

	Parm *m = &m_parms [ 0 ];

	CollectionRec cr;
	SearchInput   si;

	///////////////////////////////////////////
	// CAN ONLY BE CHANGED IN CONF AT STARTUP (no cgi field)
	///////////////////////////////////////////

	char *g = (char *)&g_conf;
	char *x = (char *)&cr;
	char *y = (char *)&si;

	// just a comment in the conf file
	m->m_desc  = 
		"All <, >, \" and # characters that are values for a field "
		"contained herein must be represented as "
		"&lt;, &gt;, &#34; and &#035; respectively.";
	m->m_type  = TYPE_COMMENT;
	m->m_page  = PAGE_NONE;
	m->m_obj   = OBJ_CONF;
	m++;

	// if the next guy has no description (m_desc) he is assumed to
	// share the description of the previous parm with one.
	/*
	m->m_title = "main external ip";
	m->m_desc  = "This is the IP and port that a user connects to in "
		"order to search this Gigablast network. This should be the "
		"same for all gb processes.";
	m->m_off   = (char *)&g_conf.m_mainExternalIp - g;
	m->m_def   = "127.0.0.1"; // if no default, it is required!
	m->m_type  = TYPE_IP;
	m++;

	m->m_title = "main external port";
	m->m_desc  = "";
	m->m_off   = (char *)&g_conf.m_mainExternalPort - g;
	m->m_def   = "80";
	m->m_type  = TYPE_LONG;
	m++;
	*/

	m->m_title = "max mem";
	m->m_desc  = "Mem available to this process. May be exceeded due "
		"to fragmentation.";
	m->m_off   = (char *)&g_conf.m_maxMem - g;
	m->m_def   = "4000000000";
	m->m_cgi   = "maxmem";
	m->m_type  = TYPE_LONG_LONG;
	m++;

	/*
	m->m_title = "indexdb split";
	m->m_desc  = "Number of times to split indexdb across groups. "
		"Must be a power of 2.";
	m->m_off   = (char *)&g_hostdb.m_indexSplits - g;
	// -1 means to do a full split just based on docid, just like titledb
	m->m_def   = "-1"; // "1";
	m->m_type  = TYPE_LONG;
	m++;

	m->m_title = "full indexdb split";
	m->m_desc  = "Set to 1 (true) if indexdb is fully split. Performance "
		"is much better for fully split indexes.";
	m->m_off   = (char *)&g_conf.m_fullSplit - g;
	m->m_def   = "0";
	m->m_type  = TYPE_BOOL;
	m++;

	m->m_title = "legacy indexdb split";
	m->m_desc  = "Set to 1 (true) if using legacy indexdb splitting.  For "
		"data generated with farmington release.";
	m->m_off   = (char *)&g_conf.m_legacyIndexdbSplit - g;
	m->m_def   = "0";
	m->m_type  = TYPE_BOOL;
	m++;

	m->m_title = "tfndb extension bits";
	m->m_desc  = "Number of extension bits to use in Tfndb.  Increased for "
		"large indexes.";
	m->m_off   = (char *)&g_conf.m_tfndbExtBits - g;
	m->m_def   = "7";
	m->m_type  = TYPE_LONG;
	m++;
	*/

	/*
	m->m_title = "checksumdb key size";
	m->m_desc  = "This determines the key size for checksums. " 
		     "Must be set for every host.";
	//m->m_cgi   = "";
	m->m_off   = (char *)&g_conf.m_checksumdbKeySize - g;
	m->m_type  = TYPE_LONG;
	m->m_def   = "12";
	m++;
	*/

	// just a comment in the conf file
	m->m_desc  = 
    "Below the various Gigablast databases are configured.\n"
    "<*dbMaxTreeMem>          - mem used for holding new recs\n"
    "<*dbMaxDiskPageCacheMem> - disk page cache mem for this db\n"
    "<*dbMaxCacheMem>         - cache mem for holding single recs\n"
//"<*dbMinFilesToMerge>     - required # files to trigger merge\n"
    "<*dbSaveCache>           - save the rec cache on exit?\n"
    "<*dbMaxCacheAge>         - max age (seconds) for recs in rec cache\n"
		"See that Stats page for record counts and stats.\n";
	m->m_type  = TYPE_COMMENT;
	m++;

	m->m_title = "dns max cache mem";
	m->m_desc  = "How many bytes should be used for caching DNS replies?";
	m->m_off   = (char *)&g_conf.m_dnsMaxCacheMem - g;
	m->m_def   = "128000";
	m->m_type  = TYPE_LONG;
	m->m_flags = PF_NOSYNC;
	m++;

	// g_dnsDistributed always saves now. main.cpp inits it that way.
	//m->m_title = "dns save cache";
	//m->m_desc  = "Should the DNS reply cache be saved/loaded on "
	//	"exit/startup?";
	//m->m_off   = (char *)&g_conf.m_dnsSaveCache - g;
	//m->m_def   = "0";
	//m->m_type  = TYPE_BOOL;
	//m++;

	m->m_title = "tagdb max tree mem";
	m->m_desc  = "A tagdb record "
		"assigns a url or site to a ruleset. Each tagdb record is "
		"about 100 bytes or so.";
	m->m_off   = (char *)&g_conf.m_tagdbMaxTreeMem - g;
	m->m_def   = "1028000"; 
	m->m_type  = TYPE_LONG;
	m->m_flags = PF_NOSYNC;
	m++;

	m->m_title = "tagdb max page cache mem";
	m->m_desc  = "";
	m->m_off   = (char *)&g_conf.m_tagdbMaxDiskPageCacheMem - g;
	m->m_def   = "200000"; 
	m->m_type  = TYPE_LONG;
	m->m_flags = PF_NOSYNC;
	m++;

	//m->m_title = "tagdb max cache mem";
	//m->m_desc  = "";
	//m->m_off   = (char *)&g_conf.m_tagdbMaxCacheMem - g;
	//m->m_def   = "128000"; 
	//m->m_type  = TYPE_LONG;
	//m++;

	//m->m_title = "tagdb min files to merge";
	//m->m_desc  = "";
	//m->m_off   = (char *)&g_conf.m_tagdbMinFilesToMerge - g;
	//m->m_def   = "2"; 
	//m->m_type  = TYPE_LONG;
	//m->m_save  = 0;
	//m++;

	m->m_title = "catdb max tree mem";
	m->m_desc  = "A catdb record "
		"assigns a url or site to DMOZ categories. Each catdb record "
		"is about 100 bytes.";
	m->m_off   = (char *)&g_conf.m_catdbMaxTreeMem - g;
	m->m_def   = "1000000"; 
	m->m_type  = TYPE_LONG;
	m->m_flags = PF_NOSYNC;
	m++;

	m->m_title = "catdb max page cache mem";
	m->m_desc  = "";
	m->m_off   = (char *)&g_conf.m_catdbMaxDiskPageCacheMem - g;
	m->m_def   = "25000000";
	m->m_type  = TYPE_LONG;
	m->m_flags = PF_NOSYNC;
	m++;

	m->m_title = "catdb max cache mem";
	m->m_desc  = "";
	m->m_off   = (char *)&g_conf.m_catdbMaxCacheMem - g;
	m->m_def   = "0"; 
	m->m_type  = TYPE_LONG;
	m->m_flags = PF_NOSYNC;
	m++;

	/*
	m->m_title = "catdb min files to merge";
	m->m_desc  = "";
	m->m_off   = (char *)&g_conf.m_catdbMinFilesToMerge - g;
	m->m_def   = "2"; 
	m->m_type  = TYPE_LONG;
	m->m_save  = 0;
	m++;

	m->m_title = "revdb max tree mem";
	m->m_desc  = "Revdb holds the meta list we added for this doc.";
	m->m_off   = (char *)&g_conf.m_revdbMaxTreeMem - g;
	m->m_def   = "30000000"; 
	m->m_type  = TYPE_LONG;
	m++;
	*/

	/*
	m->m_title = "timedb max tree mem";
	m->m_desc  = "Timedb holds event time intervals";
	m->m_off   = (char *)&g_conf.m_timedbMaxTreeMem - g;
	m->m_def   = "30000000"; 
	m->m_type  = TYPE_LONG;
	m++;
	*/

	/*
	m->m_title = "titledb max tree mem";
	m->m_desc  = "Titledb holds the compressed documents that have been "
		"indexed.";
	m->m_off   = (char *)&g_conf.m_titledbMaxTreeMem - g;
	m->m_def   = "10000000"; 
	m->m_type  = TYPE_LONG;
	m++;

	m->m_title = "titledb max cache mem";
	m->m_desc  = "";
	m->m_off   = (char *)&g_conf.m_titledbMaxCacheMem - g;
	m->m_def   = "1000000"; 
	m->m_type  = TYPE_LONG;
	m++;

	m->m_title = "titledb max cache age";
	m->m_desc  = "";
	m->m_off   = (char *)&g_conf.m_titledbMaxCacheAge - g;
	m->m_def   = "86400";  // 1 day
	m->m_type  = TYPE_LONG;
	m++;

	m->m_title = "titledb save cache";
	m->m_desc  = "";
	m->m_off   = (char *)&g_conf.m_titledbSaveCache - g;
	m->m_def   = "0"; 
	m->m_type  = TYPE_BOOL;
	m++;
	*/

	m->m_title = "clusterdb max tree mem";
	m->m_desc  = "Clusterdb caches small records for site clustering "
		"and deduping.";
	m->m_off   = (char *)&g_conf.m_clusterdbMaxTreeMem - g;
	m->m_def   = "1000000";
	m->m_type  = TYPE_LONG;
	m->m_flags = PF_NOSYNC;
	m++;

	/*
	m->m_title = "clusterdb max cache mem";
	m->m_desc  = "";
	m->m_off   = (char *)&g_conf.m_clusterdbMaxCacheMem - g;
	m->m_def   = "100000000"; 
	m->m_type  = TYPE_LONG;
	m++;

	m->m_title = "clusterdb max page cache mem";
	m->m_desc  = "";
	m->m_off =(char *)&g_conf.m_clusterdbMaxDiskPageCacheMem - g;
	m->m_def   = "100000000"; 
	m->m_type  = TYPE_LONG;
	m++;
	*/

	// this is overridden by collection
	m->m_title = "clusterdb min files to merge";
	m->m_desc  = "";
	m->m_cgi   = "cmftm";
	m->m_off   = (char *)&g_conf.m_clusterdbMinFilesToMerge - g;
	//m->m_def   = "2";
	m->m_def   = "-1"; // -1 means to use collection rec
	m->m_type  = TYPE_LONG;
	m->m_save  = 0;
	m++;

	m->m_title = "clusterdb save cache";
	m->m_desc  = "";
	m->m_cgi   = "cdbsc";
	m->m_off   = (char *)&g_conf.m_clusterdbSaveCache - g;
	m->m_def   = "0"; 
	m->m_type  = TYPE_BOOL;
	m++;

	m->m_title = "max vector cache mem";
	m->m_desc  = "Max memory for dup vector cache.";
	m->m_off   = (char *)&g_conf.m_maxVectorCacheMem - g;
	m->m_def   = "10000000";
	m->m_type  = TYPE_LONG;
	m->m_flags = PF_NOSYNC;
	m++;

	/*
	m->m_title = "checksumdb max tree mem";
	m->m_desc  = "Checksumdb is used for deduping same-site urls at "
		"index time.";
	m->m_off   = (char *)&g_conf.m_checksumdbMaxTreeMem - g;
	m->m_def   = "1000000"; 
	m->m_type  = TYPE_LONG;
	m++;

	m->m_title = "checksumdb max cache mem";
	m->m_desc  = "";
	m->m_off   = (char *)&g_conf.m_checksumdbMaxCacheMem - g;
	m->m_def   = "2000000"; 
	m->m_type  = TYPE_LONG;
	m++;

	m->m_title = "checksumdb max page cache mem";
	m->m_desc  = "";
	m->m_off =(char *)&g_conf.m_checksumdbMaxDiskPageCacheMem-g;
	m->m_def   = "1000000"; 
	m->m_type  = TYPE_LONG;
	m++;

	// this is overridden by collection
	m->m_title = "checksumdb min files to merge";
	m->m_desc  = "";
	m->m_off   = (char *)&g_conf.m_checksumdbMinFilesToMerge- g;
	//m->m_def   = "2"; 
	m->m_def   = "-1"; // -1 means to use collection rec
	m->m_type  = TYPE_LONG;
	m->m_save  = 0;
	m++;
	*/

	/*
	m->m_title = "tfndb max tree mem";
	m->m_desc  = "Tfndb holds small records for each url in Spiderdb or "
		"Titledb.";
	m->m_off   = (char *)&g_conf.m_tfndbMaxTreeMem - g;
	m->m_def   = "1000000"; 
	m->m_type  = TYPE_LONG;
	m++;

	m->m_title = "tfndb max page cache mem";
	m->m_desc  = "";
	m->m_off   = (char *)&g_conf.m_tfndbMaxDiskPageCacheMem - g;
	m->m_def   = "5000000"; 
	m->m_type  = TYPE_LONG;
	m++;
	*/

	/*
	// this is overridden by collection
	m->m_title = "tfndb min files to merge";
	m->m_desc  = "";
	m->m_off   = (char *)&g_conf.m_tfndbMinFilesToMerge - g;
	m->m_def   = "2"; 
	m->m_type  = TYPE_LONG;
	m->m_save  = 0;
	m++;
	*/

	/*
	m->m_title = "spiderdb max tree mem";
	m->m_desc  = "Spiderdb holds urls to be spidered.";
	m->m_off   = (char *)&g_conf.m_spiderdbMaxTreeMem - g;
	m->m_def   = "1000000"; 
	m->m_type  = TYPE_LONG;
	m++;

	m->m_title = "spiderdb max cache mem";
	m->m_desc  = "";
	m->m_off   = (char *)&g_conf.m_spiderdbMaxCacheMem - g;
	m->m_def   = "0"; 
	m->m_type  = TYPE_LONG;
	m++;

	m->m_title = "spiderdb max page cache mem";
	m->m_desc  = "";
	m->m_off   =(char *)&g_conf.m_spiderdbMaxDiskPageCacheMem-g;
	m->m_def   = "500000"; 
	m->m_type  = TYPE_LONG;
	m++;

	// this is overridden by collection
	m->m_title = "spiderdb min files to merge";
	m->m_desc  = "";
	m->m_off   = (char *)&g_conf.m_spiderdbMinFilesToMerge - g;
	//m->m_def   = "2"; 
	m->m_def   = "-1"; // -1 means to use collection rec
	m->m_type  = TYPE_LONG;
	m->m_save  = 0;
	m++;
	*/

	m->m_title = "robotdb max cache mem";
	m->m_desc  = "Robotdb caches robot.txt files.";
	m->m_off   = (char *)&g_conf.m_robotdbMaxCacheMem - g;
	m->m_def   = "128000"; 
	m->m_type  = TYPE_LONG;
	m->m_flags = PF_NOSYNC;
	m++;

	m->m_title = "robotdb save cache";
	m->m_cgi   = "rdbsc";
	m->m_desc  = "";
	m->m_off   = (char *)&g_conf.m_robotdbSaveCache - g;
	m->m_def   = "0"; 
	m->m_type  = TYPE_BOOL;
	m++;

	/*
	m->m_title = "indexdb max tree mem";
	m->m_desc  = "Indexdb holds the terms extracted from spidered "
		"documents."; 
	m->m_off   = (char *)&g_conf.m_indexdbMaxTreeMem - g;
	m->m_def   = "10000000";
	m->m_type  = TYPE_LONG;
	m++;

	m->m_title = "indexdb max cache mem";
	m->m_desc  = "";
	m->m_off   = (char *)&g_conf.m_indexdbMaxCacheMem - g;
	m->m_def   = "5000000";
	m->m_type  = TYPE_LONG;
	m++;

	m->m_title = "indexdb max page cache mem";
	m->m_desc  = "";
	m->m_off   = (char *)&g_conf.m_indexdbMaxDiskPageCacheMem - g;
	m->m_def   = "50000000";
	m->m_type  = TYPE_LONG;
	m++;
	*/

	m->m_title = "linkdb max page cache mem";
	m->m_desc  = "";
	m->m_off   = (char *)&g_conf.m_linkdbMaxDiskPageCacheMem - g;
	m->m_def   = "0";
	m->m_type  = TYPE_LONG;
	m->m_flags = PF_NOSYNC;
	m++;

	/*
	// this is overridden by collection
	m->m_title = "indexdb min files to merge";
	m->m_desc  = "";
	m->m_off   = (char *)&g_conf.m_indexdbMinFilesToMerge - g;
	//m->m_def   = "6"; 
	m->m_def   = "-1"; // -1 means to use collection rec
	m->m_type  = TYPE_LONG;
	m->m_save  = 0;
	m++;

	m->m_title = "indexdb max index list age";
	m->m_desc  = "";
	m->m_off   = (char *)&g_conf.m_indexdbMaxIndexListAge - g;
	m->m_def   = "60"; 
	m->m_type  = TYPE_LONG;
	m++;

	//m->m_title = "indexdb truncation limit";
	//m->m_desc  = "";
	//m->m_off   = (char *)&g_conf.m_indexdbTruncationLimit - g;
	//m->m_def   = "50000000"; 
	//m->m_type  = TYPE_LONG;
	//m++;

	m->m_title = "indexdb save cache";
	m->m_desc  = "";
	m->m_off   = (char *)&g_conf.m_indexdbSaveCache - g;
	m->m_def   = "0"; 
	m->m_type  = TYPE_BOOL;
	m++;
	*/

	/*
	m->m_title = "datedb max tree mem";
	m->m_desc  = "Datedb holds the terms extracted from spidered "
		"documents."; 
	m->m_off   = (char *)&g_conf.m_datedbMaxTreeMem - g;
	m->m_def   = "10000000";
	m->m_type  = TYPE_LONG;
	m++;

	m->m_title = "datedb max cache mem";
	m->m_desc  = "";
	m->m_off   = (char *)&g_conf.m_datedbMaxCacheMem - g;
	m->m_def   = "1000000";
	m->m_type  = TYPE_LONG;
	m++;

	// this is overridden by collection
	m->m_title = "datedb min files to merge";
	m->m_desc  = "";
	m->m_off   = (char *)&g_conf.m_datedbMinFilesToMerge - g;
	//m->m_def   = "8"; 
	m->m_def   = "-1"; // -1 means to use collection rec
	m->m_type  = TYPE_LONG;
	m->m_save  = 0;
	m++;

	m->m_title = "datedb max index list age";
	m->m_desc  = "";
	m->m_off   = (char *)&g_conf.m_datedbMaxIndexListAge - g;
	m->m_def   = "60"; 
	m->m_type  = TYPE_LONG;
	m++;

	m->m_title = "datedb save cache";
	m->m_desc  = "";
	m->m_off   = (char *)&g_conf.m_datedbSaveCache - g;
	m->m_def   = "0"; 
	m->m_type  = TYPE_BOOL;
	m++;
	*/

	/*
	m->m_title = "linkdb max tree mem";
	m->m_desc  = "Linkdb stores linking information";
	m->m_off   = (char *)&g_conf.m_linkdbMaxTreeMem - g;
	m->m_def   = "20000000";
	m->m_type  = TYPE_LONG;
	m++;

	// this is overridden by collection
	m->m_title = "linkdb min files to merge";
	m->m_desc  = "";
	m->m_off   = (char *)&g_conf.m_linkdbMinFilesToMerge - g;
  	m->m_def   = "-1"; // -1 means to use collection rec
	m->m_type  = TYPE_LONG;
	//m->m_save  = 0;
	m++;
	*/

	/*
	m->m_title = "quota table max mem";
	m->m_desc  = "For caching and keeping tabs on exact quotas per "
		"domain without having to do a disk seek. If you are using "
		"exact quotas and see a lot of disk seeks on Indexdb, try "
		"increasing this.";
	m->m_off   = (char *)&g_conf.m_quotaTableMaxMem - g;
	m->m_def   = "1000000";
	m->m_type  = TYPE_LONG;
	m++;
	*/

	m->m_title = "statsdb max tree mem";
	m->m_desc  = "";
	m->m_off   = (char *)&g_conf.m_statsdbMaxTreeMem - g;
	m->m_def   = "5000000";
	m->m_type  = TYPE_LONG;
	m->m_flags = PF_NOSYNC;
	m++;

	m->m_title = "statsdb max cache mem";
	m->m_desc  = "";
	m->m_off   = (char *)&g_conf.m_statsdbMaxCacheMem - g;
	m->m_def   = "0";
	m->m_type  = TYPE_LONG;
	m->m_flags = PF_NOSYNC;
	m++;

	m->m_title = "statsdb max disk page cache mem";
	m->m_desc  = "";
	m->m_off   = (char *)&g_conf.m_statsdbMaxDiskPageCacheMem - g;
	m->m_def   = "1000000";
	m->m_type  = TYPE_LONG;
	m->m_flags = PF_NOSYNC;
	m++;

	//m->m_title = "statsdb min files to merge";
	//m->m_desc  = "";
	//m->m_off   = (char *)&g_conf.m_statsdbMinFilesToMerge - g;
	//m->m_def   = "5";
	//m->m_type  = TYPE_LONG;
	//m++;


	/*
	m->m_title = "use buckets for in memory recs";
	m->m_desc  = "Use buckets for in memory recs for indexdb, datedb, "
		"and linkdb.";
	m->m_off   = (char *)&g_conf.m_useBuckets - g;
	m->m_def   = "1"; 
	m->m_type  = TYPE_BOOL;
	m++;
	*/


	m->m_title = "http max send buf size";
	m->m_desc  = "Maximum bytes of a doc that can be sent before having "
		"to read more from disk";
	m->m_cgi   = "hmsbs";
	m->m_off   = (char *)&g_conf.m_httpMaxSendBufSize - g;
	m->m_def   = "128000"; 
	m->m_type  = TYPE_LONG;
	m++;

	m->m_title = "search results max cache mem";
	m->m_desc  = "Bytes to use for caching search result pages.";
	m->m_off   = (char *)&g_conf.m_searchResultsMaxCacheMem - g;
	m->m_def   = "100000"; 
	m->m_type  = TYPE_LONG;
	m->m_flags = PF_NOSYNC;
	m++;

	//m->m_title = "search results max cache age";
	//m->m_desc  = "Maximum age to cache search results page in seconds.";
	//m->m_off   = (char *)&g_conf.m_searchResultsMaxCacheAge - g;
	//m->m_def   = "86400"; 
	//m->m_type  = TYPE_LONG;
	//m++;

	//m->m_title = "search results save cache";
	//m->m_desc  = "Should the search results cache be saved to disk?";
	//m->m_off   = (char *)&g_conf.m_searchResultsSaveCache - g;
	//m->m_def   = "0"; 
	//m->m_type  = TYPE_BOOL;
	//m++;

	//m->m_title = "site link info max cache mem";
	//m->m_desc  = "Bytes to use for site link info data.";
	//m->m_off   = (char *)&g_conf.m_siteLinkInfoMaxCacheMem - g;
	//m->m_def   = "100000";
	//m->m_type  = TYPE_LONG;
	//m++;

	//m->m_title = "site link info max cache age";
	//m->m_desc  = "Maximum age to cache site link info data in seconds.";
	//m->m_off   = (char *)&g_conf.m_siteLinkInfoMaxCacheAge - g;
	//m->m_def   = "3600";
	//m->m_type  = TYPE_LONG;
	//m++;

	//m->m_title = "site link info save cache";
	//m->m_desc  = "Should the site link info cache be saved to disk?";
	//m->m_off   = (char *)&g_conf.m_siteLinkInfoSaveCache - g;
	//m->m_def   = "0";
	//m->m_type  = TYPE_BOOL;
	//m++;

	//m->m_title = "site quality max cache mem";
	//m->m_desc  = "Bytes to use for site or root page quality.";
	//m->m_off   = (char *)&g_conf.m_siteQualityMaxCacheMem - g;
	//m->m_def   = "2000000"; // 2MB
	//m->m_type  = TYPE_LONG;
	//m++;

	//m->m_title = "site quality save cache";
	//m->m_desc  = "Should the site link info cache be saved to disk?";
	//m->m_off   = (char *)&g_conf.m_siteQualitySaveCache - g;
	//m->m_def   = "0";
	//m->m_type  = TYPE_BOOL;
	//m++;

	//m->m_title = "max incoming links to sample";
	//m->m_desc  = "Max linkers to a doc that are sampled to determine "
	//	"quality and for gathering link text.";
	//m->m_off   = (char *)&g_conf.m_maxIncomingLinksToSample - g;
	//m->m_def   = "100"; 
	//m->m_type  = TYPE_LONG;
	//m++;

	//m->m_title = "allow async signals";
	//m->m_desc  = "Allow software interrupts?";
	//m->m_off   = (char *)&g_conf.m_allowAsyncSignals - g;
	//m->m_def   = "1"; 
	//m->m_type  = TYPE_BOOL;
	//m++;

	m->m_title = "read only mode";
	m->m_desc  = "Read only mode does not allow spidering.";
	m->m_cgi   = "readonlymode";
	m->m_off   = (char *)&g_conf.m_readOnlyMode - g;
	m->m_def   = "0"; 
	m->m_type  = TYPE_BOOL;
	m++;

	/*
	  Disable this until it works.
	m->m_title = "use merge token";
	m->m_desc  = "Restrict merging to one host per token group? Hosts "
		"that use the same disk and mirror hosts are generally in the "
		"same token group so that only one host in the group can be "
		"doing a merge at a time. This prevents query response time "
		"from suffering too much.";
	m->m_off   = (char *)&g_conf.m_useMergeToken - g;
	m->m_def   = "1"; 
	m->m_type  = TYPE_BOOL;
	m++;
	*/

	m->m_title = "do spell checking";
	m->m_desc  = "Spell check using the dictionary.";
	m->m_off   = (char *)&g_conf.m_doSpellChecking - g;
	m->m_cgi   = "dospellchecking";
	m->m_def   = "1"; 
	m->m_type  = TYPE_BOOL;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "do narrow search";
	m->m_desc  = "give narrow search suggestions.";
	m->m_off   = (char *)&g_conf.m_doNarrowSearch - g;
	m->m_cgi   = "donarrowsearch";
	m->m_def   = "0"; 
	m->m_type  = TYPE_BOOL;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	///////////////////////////////////////////
	// MASTER CONTROLS
	///////////////////////////////////////////

	m->m_title = "spidering enabled";
	m->m_desc  = "Controls all spidering for all collections";
	m->m_cgi   = "se";
	m->m_off   = (char *)&g_conf.m_spideringEnabled - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	//m->m_cast  = 0;
	m->m_page  = PAGE_MASTER;
	m++;

	m->m_title = "max total spiders";
	m->m_desc  = "What is the maximum number of web "
		"pages the spider is allowed to download "
		"simultaneously for ALL collections PER HOST?";
	m->m_cgi   = "mtsp";
	m->m_off   = (char *)&g_conf.m_maxTotalSpiders - g;
	m->m_type  = TYPE_LONG;
	m->m_def   = "100";
	m->m_group = 0;
	m++;

	/*
	m->m_title = "web spidering enabled";
	m->m_desc  = "Spiders events on web";
	m->m_cgi   = "wse";
	m->m_off   = (char *)&g_conf.m_webSpideringEnabled - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m++;
	*/

	m->m_title = "add url enabled";
	m->m_desc  = "Can people use the add url interface to add urls "
		"to the index?";
	m->m_cgi   = "ae";
	m->m_off   = (char *)&g_conf.m_addUrlEnabled - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	//m->m_cast  = 0;
	m++;

	m->m_title = "auto save frequency";
	m->m_desc  = "Save data in memory to disk after this many minutes "
		"have passed without the data having been dumped or saved "
		"to disk. Use 0 to disable.";
	m->m_cgi   = "asf";
	m->m_off   = (char *)&g_conf.m_autoSaveFrequency - g;
	m->m_type  = TYPE_LONG;
	m->m_def   = "5";
	m->m_units = "mins";
	m++;

	m->m_title = "max http sockets";
	m->m_desc  = "Maximum sockets available to serve incoming HTTP "
		"requests. Too many outstanding requests will increase "
		"query latency. Excess requests will simply have their "
		"sockets closed.";
	m->m_cgi   = "ms";
	m->m_off   = (char *)&g_conf.m_httpMaxSockets - g;
	m->m_type  = TYPE_LONG;
	m->m_def   = "100";
	m++;

	m->m_title = "max https sockets";
	m->m_desc  = "Maximum sockets available to serve incoming HTTPS "
		"requests. Like max http sockets, but for secure sockets.";
	m->m_cgi   = "mss";
	m->m_off   = (char *)&g_conf.m_httpsMaxSockets - g;
	m->m_type  = TYPE_LONG;
	m->m_def   = "100";
	m->m_group = 0;
	m++;

	m->m_title = "spider user agent";
	m->m_desc  = "Identification seen by web servers when "
		"the Gigablast spider downloads their web pages. "
		"It is polite to insert a contact email address here so "
		"webmasters that experience problems from the Gigablast "
		"spider have somewhere to vent.";
	m->m_cgi   = "sua";
	m->m_off   = (char *)&g_conf.m_spiderUserAgent - g;
	m->m_type  = TYPE_STRING;
	m->m_size  = USERAGENTMAXSIZE;
	m->m_def   = "GigablastOpenSource/1.0";
	m++;

        m->m_title = "use temporary cluster";
        m->m_desc  = "Used by proxy to point to a temporary cluster while the "
		"original cluster is updated with a new binary. The "
		"temporary cluster is the same as the original cluster but "
		"the ports are all incremented by one from what is in "
		"the hosts.conf. This should ONLY be used for the proxy.";
        m->m_cgi   = "aotp";
        m->m_off   = (char *)&g_conf.m_useTmpCluster - g;
        m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
        m++;

	/*
	m->m_title = "url injection enabled";
	m->m_desc  = "If enabled you can directly inject URLs into the index.";
	m->m_cgi   = "ie";
	m->m_off   = (char *)&g_conf.m_injectionEnabled - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m++;
	*/

	m->m_title = "init parser test run";
	m->m_desc  = "If enabled gb injects the urls in the "
		"./test-parser/urls.txt "
		"file and outputs ./test-parser/qa.html";
	m->m_cgi   = "qaptei";
	m->m_type  = TYPE_CMD;
	m->m_func  = CommandParserTestInit;
	m->m_def   = "1";
	m->m_cast  = 1;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;


	m->m_title = "init spider test run";
	m->m_desc  = "If enabled gb injects the urls in "
		"./test-spider/spider.txt "
		"and spiders links.";
	m->m_cgi   = "qasptei";
	m->m_type  = TYPE_CMD;
	m->m_func  = CommandSpiderTestInit;
	m->m_def   = "1";
	m->m_cast  = 1;
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "continue spider test run";
	m->m_desc  = "Resumes the test.";
	m->m_cgi   = "qaspter";
	m->m_type  = TYPE_CMD;
	m->m_func  = CommandSpiderTestCont;
	m->m_def   = "1";
	m->m_cast  = 1;
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	/*
	m->m_title = "do docid range splitting";
	m->m_desc  = "Split msg39 docids into ranges to save mem?";
	m->m_cgi   = "ddrs";
	m->m_off   = (char *)&g_conf.m_doDocIdRangeSplitting - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_group = 0;
	m++;
	*/

	m->m_title = "qa search test enabled";
	m->m_desc  = "If enabled gb does the search queries in "
		"./test-search/queries.txt and compares to the last run and "
		"outputs the diffs for inspection and validation.";
	m->m_cgi   = "qasste";
	m->m_off   = (char *)&g_conf.m_testSearchEnabled - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	//m->m_cast  = 0;
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	/*
	m->m_title = "just save";
	m->m_desc  = "Copies the data in memory to disk for just this host. "
		"Does Not exit.";
	m->m_cgi   = "js";
	m->m_type  = TYPE_CMD;
	m->m_func  = CommandJustSave;
	m->m_page  = PAGE_MASTER;
	m->m_cast  = 0;
	m++;
	*/

	m->m_title = "save";
	m->m_desc  = "Saves in-memory data for ALL hosts. Does Not exit.";
	m->m_cgi   = "js";
	m->m_type  = TYPE_CMD;
	m->m_func  = CommandJustSave;
	m++;

	/*
	m->m_title = "all spiders on";
	m->m_desc  = "Enable spidering on all hosts";
	m->m_cgi   = "ase";
	m->m_def   = "1";
	m->m_off   = (char *)&g_conf.m_spideringEnabled - g;
	m->m_type  = TYPE_BOOL2; // no yes or no, just a link
	m++;

	m->m_title = "all spiders off";
	m->m_desc  = "Disable spidering on all hosts";
	m->m_cgi   = "ase";
	m->m_def   = "0";
	m->m_off   = (char *)&g_conf.m_spideringEnabled - g;
	m->m_type  = TYPE_BOOL2; // no yes or no, just a link
	m++;
	*/

	/*
	m->m_title = "save & exit";
	m->m_desc  = "Copies the data in memory to disk for just this host "
		"and then shuts down the gb process.";
	m->m_cgi   = "save";
	m->m_type  = TYPE_CMD;
	m->m_func  = CommandSaveAndExit;
	m->m_cast  = 0;
	m++;

	m->m_title = "urgent save & exit";
	m->m_desc  = "Copies the data in memory to disk for just this host "
		"and then shuts down the gb process.";
	m->m_cgi   = "usave";
	m->m_type  = TYPE_CMD;
	m->m_func  = CommandUrgentSaveAndExit;
	m->m_cast  = 0;
	m->m_priv  = 4;
	m++;
	*/

	m->m_title = "save & exit";
	m->m_desc  = "Saves the data and exits for ALL hosts.";
	m->m_cgi   = "save";
	m->m_type  = TYPE_CMD;
	m->m_func  = CommandSaveAndExit;
	m->m_group = 0;
	m++;

	m->m_title = "rebalance shards";
	m->m_desc  = "Tell all hosts to scan all records in all databases, "
		"and move "
		"records to the shard they belong to. You only need to run "
		"this if Gigablast tells you to, when you are changing "
		"hosts.conf to add or remove more nodes/hosts.";
	m->m_cgi   = "rebalance";
	m->m_type  = TYPE_CMD;
	m->m_func  = CommandRebalance;
	m->m_group = 0;
	m++;

	m->m_title = "dump to disk";
	m->m_desc  = "Flushes all records in memory to the disk on all hosts.";
	m->m_cgi   = "dump";
	m->m_type  = TYPE_CMD;
	m->m_func  = CommandDiskDump;
	m->m_cast  = 1;
	m++;

	m->m_title = "tight merge posdb";
	m->m_desc  = "Merges all outstanding posdb (index) files.";
	m->m_cgi   = "pmerge";
	m->m_type  = TYPE_CMD;
	m->m_func  = CommandMergePosdb;
	m->m_cast  = 1;
	m++;

	//m->m_title = "tight merge sectiondb";
	//m->m_desc  = "Merges all outstanding sectiondb files.";
	//m->m_cgi   = "smerge";
	//m->m_type  = TYPE_CMD;
	//m->m_func  = CommandMergeSectiondb;
	//m->m_cast  = 1;
	//m++;

	m->m_title = "tight merge titledb";
	m->m_desc  = "Merges all outstanding titledb (web page cache) files.";
	m->m_cgi   = "tmerge";
	m->m_type  = TYPE_CMD;
	m->m_func  = CommandMergeTitledb;
	m->m_cast  = 1;
	m->m_group = 0;
	m++;

	m->m_title = "tight merge spiderdb";
	m->m_desc  = "Merges all outstanding spiderdb files.";
	m->m_cgi   = "spmerge";
	m->m_type  = TYPE_CMD;
	m->m_func  = CommandMergeSpiderdb;
	m->m_cast  = 1;
	m->m_group = 0;
	m++;

	m->m_title = "clear kernel error message";
	m->m_desc  = "Clears the kernel error message. You must do this "
		"to stop getting email alerts for a kernel ring buffer "
		"error alert.";
	m->m_cgi   = "clrkrnerr";
	m->m_type  = TYPE_CMD;
	m->m_func  = CommandClearKernelError;
	m->m_cast  = 1;
	m++;

	m->m_title = "disk page cache off";
	m->m_desc  = "Disable all disk page caches to save mem for "
		"tmp cluster. Run "
		"gb cacheoff to do for all hosts.";
	m->m_cgi   = "dpco";
	m->m_type  = TYPE_CMD;
	m->m_func  = CommandDiskPageCacheOff;
	m->m_cast  = 1;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	//m->m_title = "http server enabled";
	//m->m_desc  = "Disable this if you do not want anyone hitting your "
	//	"http server. Admin and local IPs are still permitted, "
	//	"however.";
	//m->m_cgi   = "hse";
	//m->m_off   = (char *)&g_conf.m_httpServerEnabled - g;
	//m->m_type  = TYPE_BOOL;
	//m->m_def   = "1";
	//m++;

	/*
	m->m_title = "ad feed enabled";
	m->m_desc  = "Serves ads unless pure=1 is in cgi parms.";
	m->m_cgi   = "afe";
	m->m_off   = (char *)&g_conf.m_adFeedEnabled - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_scgi  = "ads";
	m->m_soff  = (char *)&si.m_adFeedEnabled - y;
	m->m_sparm = 1;	
	m->m_priv  = 2;
	m++;
	*/

	m->m_title = "do stripe balancing";
	m->m_desc  = "Stripe #n contains twin #n from each group. Doing "
		"stripe balancing helps prevent too many query requests "
		"coming into one host. This parm is only for the proxy. "
		"Stripe balancing is done by default unless the parm is "
		"disabled on the proxy in which case it appends a "
		"&dsb=0 to the query url it sends to the host. The proxy "
		"alternates to which host it forwards the incoming query "
		"based on the stripe. It takes the number of query terms in "
		"the query into account to make a more even balance.";
	m->m_cgi   = "dsb";
	m->m_off   = (char *)&g_conf.m_doStripeBalancing - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	//m->m_scgi  = "dsb";
	//m->m_soff  = (char *)&si.m_doStripeBalancing - y;
	//m->m_sparm = 1;	
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "is live cluster";
	m->m_desc  = "Is this cluster part of a live production cluster? "
		"If this is true we make sure that elvtune is being "
		"set properly for best performance, otherwise, gb will "
		"not startup.";
	m->m_cgi   = "live";
	m->m_off   = (char *)&g_conf.m_isLive - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	/*
	m->m_title = "is BuzzLogic";
	m->m_desc  = "Is this a BuzzLogic cluster?";
	m->m_cgi   = "isbuzz";
	m->m_off   = (char *)&g_conf.m_isBuzzLogic - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m++;
	*/

	// we use wikipedia cluster for quick categorization
	m->m_title = "is wikipedia cluster";
	m->m_desc  = "Is this cluster just used for indexing wikipedia pages?";
	m->m_cgi   = "iswiki";
	m->m_off   = (char *)&g_conf.m_isWikipedia - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;


        m->m_title = "ask for gzipped docs when downloading";
        m->m_desc  = "If this is true, gb will send Accept-Encoding: gzip "
		"to web servers when doing http downloads.";
        m->m_cgi   = "afgdwd";
        m->m_off   = (char *)&g_conf.m_gzipDownloads - g;
        m->m_type  = TYPE_BOOL;
        m->m_def   = "0";
        m++;
	
	m->m_title = "search results cache max age";
	m->m_desc = "How many seconds should we cache a search results "
		"page for?";
	m->m_cgi  = "srcma";
	m->m_off  = (char *)&g_conf.m_searchResultsMaxCacheAge - g;
	m->m_def  = "10800"; // 3 hrs
	m->m_type = TYPE_LONG;
	m->m_units = "seconds";
	m++;

	m->m_title = "autoban IPs which violate the queries per day quotas";
	m->m_desc  = "Keep track of ips which do queries, disallow "
		"non-customers from hitting us too hard.";
	m->m_cgi   = "ab";
	m->m_off   = (char *)&g_conf.m_doAutoBan - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m++;

	if ( g_isYippy ) {
	m->m_title = "Max outstanding search requests out for yippy";
	m->m_desc  = "Max outstanding search requests out for yippy";
	m->m_cgi   = "ymo";
	m->m_off   = (char *)&g_conf.m_maxYippyOut - g;
	m->m_type  = TYPE_LONG;
	m->m_def   = "150";
	m++;
	}

	m->m_title = "free queries per day ";
	m->m_desc  = "Non-customers get this many queries per day before"
		"being autobanned";
	m->m_cgi   = "nfqpd";
	m->m_off   = (char *)&g_conf.m_numFreeQueriesPerDay - g;
	m->m_type  = TYPE_LONG;
	m->m_def   = "1024";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "free queries per minute ";
	m->m_desc  = "Non-customers get this many queries per minute before"
		"being autobanned";
	m->m_cgi   = "nfqpm";
	m->m_off   = (char *)&g_conf.m_numFreeQueriesPerMinute - g;
	m->m_type  = TYPE_CHAR;
	m->m_def   = "30";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "max heartbeat delay in milliseconds";
	m->m_desc  = "If a heartbeat is delayed this many milliseconds "
		"dump a core so we can see where the CPU was. "
		"Logs 'db: missed heartbeat by %lli ms'. "
		"Use 0 or less to disable.";
	m->m_cgi   = "mhdms";
	m->m_off   = (char *)&g_conf.m_maxHeartbeatDelay - g;
	m->m_type  = TYPE_LONG;
	m->m_def   = "0";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "max delay before logging a callback or handler";
	m->m_desc  = "If a call to a message callback or message handler "
		"in the udp server takes more than this many milliseconds, "
		"then log it. "
		"Logs 'udp: Took %lli ms to call callback for msgType="
		"0x%hhx niceness=%li'. "
		"Use -1 or less to disable the logging.";
	m->m_cgi   = "mdch";
	m->m_off   = (char *)&g_conf.m_maxCallbackDelay - g;
	m->m_type  = TYPE_LONG;
	m->m_def   = "-1";
	m++;


	m->m_title = "sendmail IP";
	m->m_desc  = "We send crawlbot notification emails to this sendmail "
		"server which forwards them to the specified email address.";
		m->m_cgi   = "smip";
	m->m_off   = (char *)&g_conf.m_sendmailIp - g;
	m->m_type  = TYPE_STRING;
	m->m_def   = "10.5.54.47";
	m->m_size  = MAX_MX_LEN;
	m->m_priv  = 2;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "send email alerts";
	m->m_desc  = "Sends emails to admin if a host goes down.";
	m->m_cgi   = "sea";
	m->m_off   = (char *)&g_conf.m_sendEmailAlerts - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 2;
	m++;

	m->m_title = "delay non critical email alerts";
	m->m_desc  = "Do not send email alerts about dead hosts to "
		"anyone except sysadmin@gigablast.com between the times "
		"given below unless all the twins of the dead host are "
		"also dead. Instead, wait till after if the host "
		"is still dead. ";
	m->m_cgi   = "dnca";
	m->m_off   = (char *)&g_conf.m_delayNonCriticalEmailAlerts - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 2;
	m++;

	//m->m_title = "send email alerts to matt at tmobile 450-3518";
	//m->m_desc  = "Sends to cellphone.";
	//m->m_cgi   = "seatmt";
	//m->m_off   = (char *)&g_conf.m_sendEmailAlertsToMattTmobile - g;
	//m->m_type  = TYPE_BOOL;
	//m->m_def   = "1";
	//m->m_priv  = 2;
	//m->m_group = 0;
	//m++;

	//m->m_title = "send email alerts to matt at alltel 362-6809";
	/*
	m->m_title = "send email alerts to matt at alltel 450-3518";
	m->m_desc  = "Sends to cellphone.";
	m->m_cgi   = "seatmv";
	m->m_off   = (char *)&g_conf.m_sendEmailAlertsToMattAlltell - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_priv  = 2;
	m->m_group = 0;
	m++;

	m->m_title = "send email alerts to javier";
	m->m_desc  = "Sends to cellphone.";
	m->m_cgi   = "seatj";
	m->m_off   = (char *)&g_conf.m_sendEmailAlertsToJavier - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 2;
	m->m_group = 0;
	m++;
	*/

// 	m->m_title = "send email alerts to melissa";
// 	m->m_desc  = "Sends to cell phone.";
// 	m->m_cgi   = "seatme";
// 	m->m_off   = (char *)&g_conf.m_sendEmailAlertsToMelissa - g;
// 	m->m_type  = TYPE_BOOL;
// 	m->m_def   = "0";
// 	m->m_priv  = 2;
// 	m->m_group = 0;
// 	m++;

	/*
	m->m_title = "send email alerts to partap";
	m->m_desc  = "Sends to cell phone.";
	m->m_cgi   = "seatp";
	m->m_off   = (char *)&g_conf.m_sendEmailAlertsToPartap - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 2;
	m->m_group = 0;
	m++;
	*/

// 	m->m_title = "send email alerts to cinco";
// 	m->m_desc  = "Sends to cell phone.";
// 	m->m_cgi   = "seatc";
// 	m->m_off   = (char *)&g_conf.m_sendEmailAlertsToCinco - g;
// 	m->m_type  = TYPE_BOOL;
// 	m->m_def   = "0";
// 	m->m_priv  = 2;
// 	m->m_group = 0;
// 	m++;

	m->m_title = "cluster name";
	m->m_desc  = "Email alerts will include the cluster name";
	m->m_cgi   = "cn";
	m->m_off   = (char *)&g_conf.m_clusterName - g;
	m->m_type  = TYPE_STRING;
	m->m_size  = 32;
	m->m_def   = "unspecified";
	m++;

	m->m_title = "send email alerts to sysadmin";
	m->m_desc  = "Sends to sysadmin@gigablast.com.";
	m->m_cgi   = "seatsa";
	m->m_off   = (char *)&g_conf.m_sendEmailAlertsToSysadmin - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m->m_priv  = 2;
	m++;

	/*
	m->m_title = "send email alerts to zak";
	m->m_desc  = "Sends to zak@gigablast.com.";
	m->m_cgi   = "seatz";
	m->m_off   = (char *)&g_conf.m_sendEmailAlertsToZak - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 2;
	m->m_group = 0;
	m++;

	m->m_title = "send email alerts to sabino";
	m->m_desc  = "Sends to cell phone.";
	m->m_cgi   = "seatms";
	m->m_off   = (char *)&g_conf.m_sendEmailAlertsToSabino - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 2;
	m->m_group = 0;
	m++;
	*/

	m->m_title = "dead host timeout";
	m->m_desc  = "Consider a host in the Gigablast network to be dead if "
		"it does not respond to successive pings for this number of "
		"seconds. Gigablast does not send requests to dead hosts. "
		"Outstanding requests may be re-routed to a twin.";
	m->m_cgi   = "dht";
	m->m_off   = (char *)&g_conf.m_deadHostTimeout - g;
	m->m_type  = TYPE_LONG;
	m->m_def   = "4000";
	m->m_units = "milliseconds";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "send email timeout";
	m->m_desc  = "Send an email after a host has not responded to "
		"successive pings for this many milliseconds.";
	m->m_cgi   = "set";
	m->m_off   = (char *)&g_conf.m_sendEmailTimeout - g;
	m->m_type  = TYPE_LONG;
	m->m_def   = "62000";
	m->m_priv  = 2;
	m->m_units = "milliseconds";
	m++;

	m->m_title = "ping spacer";
	m->m_desc  = "Wait this many milliseconds before pinging the next "
		"host. Each host pings all other hosts in the network.";
	m->m_cgi   = "ps";
	m->m_off   = (char *)&g_conf.m_pingSpacer - g;
	m->m_min   = 50; // i've seen values of 0 hammer the cpu
	m->m_type  = TYPE_LONG;
	m->m_def   = "100";
	m->m_units = "milliseconds";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	//m->m_title = "max query time";
	//m->m_desc  = "When computing the avgerage query latency "
	//	"truncate query latency times to this so that "
	//	"a single insanely long query latency time does "
	//	"not trigger the alarm. This is in seconds.";
	//m->m_cgi   = "mqlr";
	//m->m_off   = (char *)&g_conf.m_maxQueryTime - g;
	//m->m_type  = TYPE_FLOAT;
	//m->m_def   = "30.0";
	//m->m_priv  = 2;
	//m->m_group = 0;
	//m++;

	m->m_title = "query success rate threshold";
	m->m_desc  = "Send email alerts when query success rate goes below "
		"this threshold. (percent rate between 0.0 and 1.0)";
	m->m_cgi   = "qsrt";
	m->m_off   = (char *)&g_conf.m_querySuccessThreshold - g;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "0.850000";
	m->m_priv  = 2;
	m++;

	m->m_title = "average query latency threshold";
	m->m_desc  = "Send email alerts when average query latency goes above "
		"this threshold. (in seconds)";
	m->m_cgi   = "aqpst";
	m->m_off   = (char *)&g_conf.m_avgQueryTimeThreshold - g;
	m->m_type  = TYPE_FLOAT;
	// a titlerec fetch times out after 2 seconds and is re-routed
	m->m_def   = "2.000000";
	m->m_priv  = 2;
	m->m_units = "seconds";
	m++;

	m->m_title = "number of query times in average";
	m->m_desc  = "Record this number of query times before calculating "
		"average query latency.";
	m->m_cgi   = "nqt";
	m->m_off   = (char *)&g_conf.m_numQueryTimes - g;
	m->m_type  = TYPE_LONG;
	m->m_def   = "300";
	m->m_priv  = 2;
	m->m_group = 0;
	m++;


	m->m_title = "max corrupt index lists";
	m->m_desc  = "If we reach this many corrupt index lists, send "
		"an admin email.  Set to -1 to disable.";
	m->m_cgi   = "mcil";
	m->m_off   = (char *)&g_conf.m_maxCorruptLists - g;
	m->m_type  = TYPE_LONG;
	m->m_def   = "5";
	m->m_priv  = 2;
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "max hard drive temperature";
	m->m_desc  = "At what temperature in Celsius should we send "
		"an email alert if a hard drive reaches it?";
	m->m_cgi   = "mhdt";
	m->m_off   = (char *)&g_conf.m_maxHardDriveTemp - g;
	m->m_type  = TYPE_LONG;
	m->m_def   = "45";
	m++;

	/*
	m->m_title = "delay emails after";
	m->m_desc  = "If delay non critical email alerts is on, don't send "
		"emails after this time. Time is hh:mm. Time is take from "
		"host #0's system clock in UTC.";
	m->m_cgi   = "dea";
	m->m_off   = (char *)&g_conf.m_delayEmailsAfter - g;
	m->m_type  = TYPE_TIME; // time format -- very special
	m->m_def   = "00:00";
	m->m_priv  = 2;
	m++;

	m->m_title = "delay emails before";
	m->m_desc  = "If delay non critical email alerts is on, don't send "
		"emails before this time. Time is hh:mm Time is take from "
		"host #0's system clock in UTC.";
	m->m_cgi   = "deb";
	m->m_off   = (char *)&g_conf.m_delayEmailsBefore - g;
	m->m_type  = TYPE_TIME; // time format -- very special
	m->m_def   = "00:00";
	m->m_priv  = 2;
	m++;
	*/


	/*
	  Disable this until it works.
	m->m_title = "use merge token";
	m->m_desc  = "If used, prevents twins, or hosts on the same ide "
		"channel, from merging simultaneously.";
	m->m_cgi   = "umt";
	m->m_off   = (char *)&g_conf.m_useMergeToken - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m++;
	*/

	m->m_title = "error string 1";
	m->m_desc  = "Look for this string in the kernel buffer for sending "
		"email alert. Useful for detecting some strange "
		"hard drive failures that really slow performance.";
	m->m_cgi   = "errstrone";
	m->m_off   = (char *)&g_conf.m_errstr1 - g;
	m->m_type  = TYPE_STRING;
	m->m_def   = "";
	m->m_size  = MAX_URL_LEN;
	m->m_priv  = 2;
	m++;

	m->m_title = "error string 2";
	m->m_desc  = "Look for this string in the kernel buffer for sending "
		"email alert. Useful for detecting some strange "
		"hard drive failures that really slow performance.";
	m->m_cgi   = "errstrtwo";
	m->m_off   = (char *)&g_conf.m_errstr2 - g;
	m->m_type  = TYPE_STRING;
	m->m_def   = "";
	m->m_size  = MAX_URL_LEN;
	m->m_priv  = 2;
	m->m_group = 0;
	m++;

	m->m_title = "error string 3";
	m->m_desc  = "Look for this string in the kernel buffer for sending "
		"email alert. Useful for detecting some strange "
		"hard drive failures that really slow performance.";
	m->m_cgi   = "errstrthree";
	m->m_off   = (char *)&g_conf.m_errstr3 - g;
	m->m_type  = TYPE_STRING;
	m->m_def   = "";
	m->m_size  = MAX_URL_LEN;
	m->m_priv  = 2;
	m->m_group = 0;
	m++;

	m->m_title = "send email alerts to email 1";
	m->m_desc  = "Sends to email address 1 through email server 1.";
	m->m_cgi   = "seatone";
	m->m_off   = (char *)&g_conf.m_sendEmailAlertsToEmail1 - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 2;
	m++;

	m->m_title = "send parm change email alerts to email 1";
	m->m_desc  = "Sends to email address 1 through email server 1 if "
		"any parm is changed.";
	m->m_cgi   = "seatonep";
	m->m_off   = (char *)&g_conf.m_sendParmChangeAlertsToEmail1 - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 2;
	m->m_group = 0;
	m++;

	m->m_title = "email server 1";
	m->m_desc  = "Connects to this server directly when sending email 1 ";
	m->m_cgi   = "esrvone";
	m->m_off   = (char *)&g_conf.m_email1MX - g;
	m->m_type  = TYPE_STRING;
	m->m_def   = "10.5.54.47";
	m->m_size  = MAX_MX_LEN;
	m->m_priv  = 2;
	m->m_group = 0;
	m++;

	m->m_title = "email address 1";
	m->m_desc  = "Sends to this address when sending email 1 ";
	m->m_cgi   = "eaddrone";
	m->m_off   = (char *)&g_conf.m_email1Addr - g;
	m->m_type  = TYPE_STRING;
	m->m_def   = "4081234567@vtext.com";
	m->m_size  = MAX_EMAIL_LEN;
	m->m_priv  = 2;
	m->m_group = 0;
	m++;

	m->m_title = "from email address 1";
	m->m_desc  = "The from field when sending email 1 ";
	m->m_cgi   = "efaddrone";
	m->m_off   = (char *)&g_conf.m_email1From - g;
	m->m_type  = TYPE_STRING;
	m->m_def   = "sysadmin@mydomain.com";
	m->m_size  = MAX_EMAIL_LEN;
	m->m_priv  = 2;
	m->m_group = 0;
	m++;

	m->m_title = "send email alerts to email 2";
	m->m_desc  = "Sends to email address 2 through email server 2.";
	m->m_cgi   = "seattwo";
	m->m_off   = (char *)&g_conf.m_sendEmailAlertsToEmail2 - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 2;
	m++;

	m->m_title = "send parm change email alerts to email 2";
	m->m_desc  = "Sends to email address 2 through email server 2 if "
		"any parm is changed.";
	m->m_cgi   = "seattwop";
	m->m_off   = (char *)&g_conf.m_sendParmChangeAlertsToEmail2 - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 2;
	m->m_group = 0;
	m++;

	m->m_title = "email server 2";
	m->m_desc  = "Connects to this server directly when sending email 2 ";
	m->m_cgi   = "esrvtwo";
	m->m_off   = (char *)&g_conf.m_email2MX - g;
	m->m_type  = TYPE_STRING;
	m->m_def   = "mail.mydomain.com";
	m->m_size  = MAX_MX_LEN;
	m->m_priv  = 2;
	m->m_group = 0;
	m++;

	m->m_title = "email address 2";
	m->m_desc  = "Sends to this address when sending email 2 ";
	m->m_cgi   = "eaddrtwo";
	m->m_off   = (char *)&g_conf.m_email2Addr - g;
	m->m_type  = TYPE_STRING;
	m->m_def   = "";
	m->m_size  = MAX_EMAIL_LEN;
	m->m_priv  = 2;
	m->m_group = 0;
	m++;

	m->m_title = "from email address 2";
	m->m_desc  = "The from field when sending email 2 ";
	m->m_cgi   = "efaddrtwo";
	m->m_off   = (char *)&g_conf.m_email2From - g;
	m->m_type  = TYPE_STRING;
	m->m_def   = "sysadmin@mydomain.com";
	m->m_size  = MAX_EMAIL_LEN;
	m->m_priv  = 2;
	m->m_group = 0;
	m++;

	m->m_title = "send email alerts to email 3";
	m->m_desc  = "Sends to email address 3 through email server 3.";
	m->m_cgi   = "seatthree";
	m->m_off   = (char *)&g_conf.m_sendEmailAlertsToEmail3 - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 2;
	m++;

	m->m_title = "send parm change email alerts to email 3";
	m->m_desc  = "Sends to email address 3 through email server 3 if "
		"any parm is changed.";
	m->m_cgi   = "seatthreep";
	m->m_off   = (char *)&g_conf.m_sendParmChangeAlertsToEmail3 - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 2;
	m->m_group = 0;
	m++;

	m->m_title = "email server 3";
	m->m_desc  = "Connects to this server directly when sending email 3 ";
	m->m_cgi   = "esrvthree";
	m->m_off   = (char *)&g_conf.m_email3MX - g;
	m->m_type  = TYPE_STRING;
	m->m_def   = "mail.mydomain.com";
	m->m_size  = MAX_MX_LEN;
	m->m_priv  = 2;
	m->m_group = 0;
	m++;

	m->m_title = "email address 3";
	m->m_desc  = "Sends to this address when sending email 3 ";
	m->m_cgi   = "eaddrthree";
	m->m_off   = (char *)&g_conf.m_email3Addr - g;
	m->m_type  = TYPE_STRING;
	m->m_def   = "";
	m->m_size  = MAX_EMAIL_LEN;
	m->m_priv  = 2;
	m->m_group = 0;
	m++;

	m->m_title = "from email address 3";
	m->m_desc  = "The from field when sending email 3 ";
	m->m_cgi   = "efaddrthree";
	m->m_off   = (char *)&g_conf.m_email3From - g;
	m->m_type  = TYPE_STRING;
	m->m_def   = "sysadmin@mydomain.com";
	m->m_size  = MAX_EMAIL_LEN;
	m->m_priv  = 2;
	m->m_group = 0;
	m++;


	m->m_title = "send email alerts to email 4";
	m->m_desc  = "Sends to email address 4 through email server 4.";
	m->m_cgi   = "seatfour";
	m->m_off   = (char *)&g_conf.m_sendEmailAlertsToEmail4 - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 2;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "send parm change email alerts to email 4";
	m->m_desc  = "Sends to email address 4 through email server 4 if "
		"any parm is changed.";
	m->m_cgi   = "seatfourp";
	m->m_off   = (char *)&g_conf.m_sendParmChangeAlertsToEmail4 - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 2;
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "email server 4";
	m->m_desc  = "Connects to this server directly when sending email 4 ";
	m->m_cgi   = "esrvfour";
	m->m_off   = (char *)&g_conf.m_email4MX - g;
	m->m_type  = TYPE_STRING;
	m->m_def   = "mail.mydomain.com";
	m->m_size  = MAX_MX_LEN;
	m->m_priv  = 2;
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "email address 4";
	m->m_desc  = "Sends to this address when sending email 4 ";
	m->m_cgi   = "eaddrfour";
	m->m_off   = (char *)&g_conf.m_email4Addr - g;
	m->m_type  = TYPE_STRING;
	m->m_def   = "";
	m->m_size  = MAX_EMAIL_LEN;
	m->m_priv  = 2;
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "from email address 4";
	m->m_desc  = "The from field when sending email 4 ";
	m->m_cgi   = "efaddrfour";
	m->m_off   = (char *)&g_conf.m_email4From - g;
	m->m_type  = TYPE_STRING;
	m->m_def   = "sysadmin@mydomain.com";
	m->m_size  = MAX_EMAIL_LEN;
	m->m_priv  = 2;
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;



	m->m_title = "prefer local reads";
	m->m_desc  = "If you have scsi drives or a slow network, say yes here "
		"to minimize data fetches across the network.";
	m->m_cgi   = "plr";
	m->m_off   = (char *)&g_conf.m_preferLocalReads - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	/*
	m->m_title = "use biased tfndb";
	m->m_desc  = "Should we always send titledb record lookup requests "
		"to a particular host in order to increase tfndb page cache "
		"hits? This bypasses load balancing and may result in "
		"slower hosts being more of a bottleneck. Keep this disabled "
		"unless you notice tfndb disk seeks slowing things down.";
	m->m_cgi   = "ubu";
	m->m_off   = (char *)&g_conf.m_useBiasedTfndb - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_group = 0;
	m++;
	*/

	m->m_title = "verify disk writes";
	m->m_desc  = "Read what was written in a verification step. Decreases "
		"performance, but may help fight disk corruption mostly on "
		"Maxtors and Western Digitals.";
	m->m_cgi   = "vdw";
	m->m_off   = (char *)&g_conf.m_verifyWrites - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	// this is ifdef'd out in Msg3.cpp for performance reasons,
	// so do it here, too
#ifdef _SANITY_CHECK_
	m->m_title = "max corrupted read retries";
	m->m_desc  = "How many times to retry disk reads that had corrupted "
		"data before requesting the list from a twin, and, if that "
		"fails, removing the bad data.";
	m->m_cgi   = "crr";
	m->m_off   = (char *)&g_conf.m_corruptRetries - g;
	m->m_type  = TYPE_LONG;
	m->m_def   = "100";
	m->m_group = 0;
	m++;
#endif

	m->m_title = "do incremental updating";
	m->m_desc  = "When reindexing a document, do not re-add data "
		"that should already be in index or clusterdb "
		"since the last time the document was indexed. Otherwise, "
		"re-add the data regardless.";
	m->m_cgi   = "oic";
	//m->m_off   = (char *)&g_conf.m_onlyAddUnchangedTermIds - g;
	m->m_off   = (char *)&g_conf.m_doIncrementalUpdating - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	// you can really screw up the index if this is false, so 
	// comment it out for now
	/*
	m->m_title = "index deletes";
	m->m_desc  = "Should we allow indexdb recs to be deleted? This is "
		"always true, except in very rare indexdb rebuilds.";
	m->m_cgi   = "id";
	m->m_off   = (char *)&g_conf.m_indexDeletes - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_group = 0;
	m++;
	*/

	m->m_title = "use etc hosts";
	m->m_desc  = "Use /etc/hosts file to resolve hostnames? the "
		"/etc/host file is reloaded every minute, so if you make "
		"a change to it you might have to wait one minute for the "
		"change to take affect.";
	m->m_cgi   = "ueh";
	m->m_off   = (char *)&g_conf.m_useEtcHosts - g;
	m->m_def   = "0"; 
	m->m_type  = TYPE_BOOL;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "twins are split";
	m->m_desc  = "If enabled, Gigablast assumes the first half of "
		"machines in hosts.conf "
		"are on a different network switch than the second half, "
		"and minimizes transmits between the switches.";
	m->m_cgi   = "stw";
	m->m_off   = (char *)&g_conf.m_splitTwins - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "do out of memory testing";
	m->m_desc  = "When enabled Gigablast will randomly fail at "
		"allocating memory. Used for testing stability.";
	m->m_cgi   = "dot";
	m->m_off   = (char *)&g_conf.m_testMem - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "do consistency testing";
	m->m_desc  = "When enabled Gigablast will make sure it reparses "
		"the document exactly the same way. It does this every "
		"1000th document anyway, but enabling this makes it do it "
		"for every document.";
	m->m_cgi   = "dct";
	m->m_off   = (char *)&g_conf.m_doConsistencyTesting - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "use shotgun";
	m->m_desc  = "If enabled, all servers must have two gigabit "
		"ethernet ports hooked up and Gigablast will round robin "
		"packets between both ethernet ports when sending to another "
		"host. Can speed up network transmissions as much as 2x.";
	m->m_cgi   = "usht";
	m->m_off   = (char *)&g_conf.m_useShotgun - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "use quickpoll";
	m->m_desc  = "If enabled, Gigablast will use quickpoll. Significantly "
		"improves performance. Only turn this off for testing.";
	m->m_cgi   = "uqp";
	m->m_off   = (char *)&g_conf.m_useQuickpoll - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

// 	m->m_title = "quickpoll core on error";
// 	m->m_desc  = "If enabled, quickpoll will terminate the process and "
// 		"generate a core file when callbacks are called with the "
// 		"wrong niceness.";
// 	m->m_cgi   = "qpoe";
// 	m->m_off   = (char *)&g_conf.m_quickpollCoreOnError - g;
// 	m->m_type  = TYPE_BOOL;
// 	m->m_def   = "1";
// 	m++;

	m->m_title = "use threads";
	m->m_desc  = "If enabled, Gigablast will use threads.";
	m->m_cgi   = "ut";
	m->m_off   = (char *)&g_conf.m_useThreads - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	// . this will leak the shared mem if the process is Ctrl+C'd
	// . that is expected behavior
	// . you can clean up the leaks using 'gb freecache 20000000'
	//   and use 'ipcs -m' to see what leaks you got
	// . generally, only the main gb should use shared mem, so
	//   keep this off for teting
	m->m_title = "use shared mem";
	m->m_desc  = "If enabled, Gigablast will use shared memory. "
		"Should really only be used on the live cluster, "
		"keep this on the testing cluster since it can "
		"leak easily.";
	m->m_cgi   = "ushm";
	m->m_off   = (char *)&g_conf.m_useSHM - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	// disable disk caches... for testing really
	/*
	m->m_title = "use disk page cache for indexdb";
	m->m_desc  = "Use disk page cache?";
	m->m_cgi   = "udpci";
	m->m_off   = (char *)&g_conf.m_useDiskPageCacheIndexdb - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m++;
	*/

	m->m_title = "use disk page cache for posdb";
	m->m_desc  = "Use disk page cache?";
	m->m_cgi   = "udpci";
	m->m_off   = (char *)&g_conf.m_useDiskPageCachePosdb - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "use disk page cache for datedb";
	m->m_desc  = "Use disk page cache?";
	m->m_cgi   = "udpcd";
	m->m_off   = (char *)&g_conf.m_useDiskPageCacheDatedb - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "use disk page cache for titledb";
	m->m_desc  = "Use disk page cache?";
	m->m_cgi   = "udpct";
	m->m_off   = (char *)&g_conf.m_useDiskPageCacheTitledb - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "use disk page cache for spiderdb";
	m->m_desc  = "Use disk page cache?";
	m->m_cgi   = "udpcs";
	m->m_off   = (char *)&g_conf.m_useDiskPageCacheSpiderdb - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	/*
	m->m_title = "use disk page cache for urldb";
	m->m_desc  = "Use disk page cache?";
	m->m_cgi   = "udpcu";
	m->m_off   = (char *)&g_conf.m_useDiskPageCacheTfndb - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_group = 0;
	m++;
	*/

	m->m_title = "use disk page cache for tagdb";
	m->m_desc  = "Use disk page cache?";
	m->m_cgi   = "udpcg";
	m->m_off   = (char *)&g_conf.m_useDiskPageCacheTagdb - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "use disk page cache for checksumdb";
	m->m_desc  = "Use disk page cache?";
	m->m_cgi   = "udpck";
	m->m_off   = (char *)&g_conf.m_useDiskPageCacheChecksumdb - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "use disk page cache for clusterdb";
	m->m_desc  = "Use disk page cache?";
	m->m_cgi   = "udpcl";
	m->m_off   = (char *)&g_conf.m_useDiskPageCacheClusterdb - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "use disk page cache for catdb";
	m->m_desc  = "Use disk page cache?";
	m->m_cgi   = "udpca";
	m->m_off   = (char *)&g_conf.m_useDiskPageCacheCatdb - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "use disk page cache for linkdb";
	m->m_desc  = "Use disk page cache?";
	m->m_cgi   = "udpcnk";
	m->m_off   = (char *)&g_conf.m_useDiskPageCacheLinkdb - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	/*
	m->m_title = "exclude link text";
	m->m_desc  = "Exclude search results that have one or more query "
		"that only appear in the incoming link text";
	m->m_cgi   = "exlt";
	m->m_off  = (char *)&g_conf.m_excludeLinkText - g;
	m->m_sparm = 1;
	m->m_soff  = (char *)&si.m_excludeLinkText - y;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_scgi  = "excludelinktext";
	m++;

	m->m_title = "exclude meta text";
	m->m_desc  = "Exclude search results that have one or more query "
		"that only appear in the meta text";
	m->m_cgi   = "exmt";
	m->m_off  = (char *)&g_conf.m_excludeMetaText - g;
	m->m_sparm = 1;
	m->m_soff  = (char *)&si.m_excludeMetaText - y;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_scgi  = "excludemetatext";
	m++;
	*/

	m->m_title = "scan all if not found";
	m->m_desc  = "Scan all titledb files if rec not found. You should "
		"keep this on to avoid corruption. Do not turn it off unless "
		"you are Matt Wells.";
	m->m_cgi   = "sainf";
	m->m_off   = (char *)&g_conf.m_scanAllIfNotFound - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "interface machine";
	m->m_desc  = "for specifying if this is an interface machine"
		     "messages are rerouted from this machine to the main"
		     "cluster set in the hosts.conf.";
	m->m_cgi   = "intmch";
	m->m_off   = (char *)&g_conf.m_interfaceMachine - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 2;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "generate vector at query time";
	m->m_desc  = "At query time, should Gigablast generate content "
		"vectors for title records lacking them? This is an "
		"expensive operation, so is really just for testing purposes.";
	m->m_cgi   = "gv";
	m->m_off   = (char *)&g_conf.m_generateVectorAtQueryTime - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;



        m->m_title = "redirect non-raw traffic";
        m->m_desc  = "If this is non empty, http traffic will be redirected "
		"to the specified address.";
        m->m_cgi   = "redir";
        m->m_off   = (char *)&g_conf.m_redirect - g;
        m->m_type  = TYPE_STRING;
	m->m_size  = MAX_URL_LEN;
        m->m_def   = "";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
        m++;

        m->m_title = "send requests to compression proxy";
        m->m_desc  = "If this is true, gb will route download requests for"
		" web pages to proxies in hosts.conf.  Proxies will"
		" download and compress docs before sending back. ";
        m->m_cgi   = "srtcp";
        m->m_off   = (char *)&g_conf.m_useCompressionProxy - g;
        m->m_type  = TYPE_BOOL;
        m->m_def   = "0";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
        m++;

        m->m_title = "synchronize proxy to cluster time";
        m->m_desc  = "Enable/disable the ability to synchronize time between "
                "the cluster and the proxy";
        m->m_cgi   = "sptct";
        m->m_off   = (char *)&g_conf.m_timeSyncProxy - g;
        m->m_type  = TYPE_BOOL;
        m->m_def   = "0";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
        m++;

	/*
        m->m_title = "use data feed account server";
        m->m_desc  = "Enable/disable the use of a remote account verification "
                "for Data Feed Customers. This should ONLY be used for the "
                "proxy.";
        m->m_cgi   = "pdfuas";
        m->m_off   = (char *)&g_conf.m_useDFAcctServer - g;
        m->m_type  = TYPE_BOOL;
        m->m_def   = "0";
        m++;

        m->m_title = "data feed server ip";
        m->m_desc  = "The ip address of the Gigablast data feed server to "
                "retrieve customer account information from. This should ONLY "
                "be used for the proxy.";
        m->m_cgi   = "pdfip";
        m->m_off   = (char *)&g_conf.m_dfAcctIp - g;
        m->m_type  = TYPE_IP;
        m->m_def   = "2130706433";
        m->m_group = 0;
        m++;

        m->m_title = "data feed server port";
        m->m_desc  = "The port of the Gigablast data feed server to retrieve "
                "customer account information from. This should ONLY be used "
                "for the proxy";
        m->m_cgi   = "pdfport";
        m->m_off   = (char *)&g_conf.m_dfAcctPort - g;
        m->m_type  = TYPE_LONG;
        m->m_def   = "8040";
        m->m_group = 0;
        m++;

        m->m_title = "data feed server collection";
        m->m_desc  = "The collection on the Gigablast data feed server to "
                "retrieve customer account information from. This should ONLY "
                "be used for the proxy.";
        m->m_cgi   = "pdfcoll";
        m->m_off   = (char *)&g_conf.m_dfAcctColl - g;
        m->m_type  = TYPE_STRING;
        m->m_size  = MAX_COLL_LEN;
        m->m_def   = "customers";
        m->m_group = 0;
        m++;
	*/

	m->m_title = "allow scaling of hosts";
	m->m_desc  = "Allows scaling up of hosts by deleting recs not in "
		"the correct group.  This should only happen why copying "
		"a set of servers to the new hosts. Otherwise corrupted "
		"data will cause a halt.";
	m->m_cgi   = "asoh";
	m->m_off   = (char *)&g_conf.m_allowScale - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "allow bypass of db validation";
	m->m_desc  = "Allows bypass of db validation so gigablast will not "
		"halt if a corrupt db is discovered durring load.  Use this "
		"when attempting to load with a collection that has known "
		"corruption.";
	m->m_cgi   = "abov";
	m->m_off   = (char *)&g_conf.m_bypassValidation - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	/*
	m->m_title = "reload language pages";
	m->m_desc  = "Reloads language specific pages.";
	m->m_cgi   = "rlpages";
	m->m_type  = TYPE_CMD;
	m->m_func  = CommandReloadLanguagePages;
	m->m_cast  = 0;
	m++;

	m->m_title = "all reload language pages";
	m->m_desc  = "Reloads language specific pages for all hosts.";
	m->m_cgi   = "rlpages";
	m->m_type  = TYPE_CMD;
	m++;
	*/

	// do we need this any more?
	/*
	m->m_title = "give up on dead hosts";
	m->m_desc  = "Give up requests to dead hosts.  Only set this when you "
		"know a host is dead and will not come back online without "
		"a restarting all hosts.  Messages will timeout on the dead "
		"host but will not error, allowing outstanding spidering to "
		"finish to the twin.";
	m->m_cgi   = "gvup";
	m->m_off   = (char *)&g_conf.m_giveupOnDeadHosts - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m++;
	*/

	/*
	m->m_title = "ask root name servers";
	m->m_desc  = "if enabled Gigablast will direct DNS requests to "
		"the root DNS servers, otherwise it will continue to "
		"send DNS queries to the bind9 servers defined in "
		"the Master Controls.";
	m->m_cgi   = "bdns";
	m->m_off   = (char *)&g_conf.m_askRootNameservers - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m++;
	*/

	/*
	m->m_title = "do dig sanity checks";
	m->m_desc  = "call dig @nameServer hostname and on timedout lookups" 
			" and see if dig also timed out";
	m->m_cgi   = "dig";
	m->m_off   = (char *)&g_conf.m_useDig - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m++;
	*/

	/*
	m->m_title = "dns root name server 1";
	m->m_desc  = "IP address of a DNS root server. Assumes UDP "
		"port 53.";
	m->m_cgi   = "rnsa";
	m->m_off   = (char *)&g_conf.m_rnsIps[0] - g;
	m->m_type  = TYPE_IP;
	m->m_def   = "192.228.79.201";
	m++;

	m->m_title = "dns root name server 2";
	m->m_desc  = "IP address of a DNS root server. Assumes UDP "
		"port 53.";
	m->m_cgi   = "rnsb";
	m->m_off   = (char *)&g_conf.m_rnsIps[1] - g;
	m->m_type  = TYPE_IP;
	m->m_def   = "192.33.4.12";
	m++;
	
	m->m_title = "dns root name server 3";
	m->m_desc  = "IP address of a DNS root server. Assumes UDP "
		"port 53.";
	m->m_cgi   = "rnsc";
	m->m_off   = (char *)&g_conf.m_rnsIps[2] - g;
	m->m_type  = TYPE_IP;
	m->m_def   = "128.8.10.90";
	m++;
	
	m->m_title = "dns root name server 4";
	m->m_desc  = "IP address of a DNS root server. Assumes UDP "
		"port 53.";
	m->m_cgi   = "rnsd";
	m->m_off   = (char *)&g_conf.m_rnsIps[3] - g;
	m->m_type  = TYPE_IP;
	m->m_def   = "192.203.230.10";
	m++;
	
	m->m_title = "dns root name server 5";
	m->m_desc  = "IP address of a DNS root server. Assumes UDP "
		"port 53.";
	m->m_cgi   = "rnse";
	m->m_off   = (char *)&g_conf.m_rnsIps[4] - g;
	m->m_type  = TYPE_IP;
	m->m_def   = "192.5.5.241";
	m++;
	
	m->m_title = "dns root name server 6";
	m->m_desc  = "IP address of a DNS root server. Assumes UDP "
		"port 53.";
	m->m_cgi   = "rnsf";
	m->m_off   = (char *)&g_conf.m_rnsIps[5] - g;
	m->m_type  = TYPE_IP;
	m->m_def   = "192.112.36.4";
	m++;
	
	m->m_title = "dns root name server 7";
	m->m_desc  = "IP address of a DNS root server. Assumes UDP "
		"port 53.";
	m->m_cgi   = "rnsg";
	m->m_off   = (char *)&g_conf.m_rnsIps[6] - g;
	m->m_type  = TYPE_IP;
	m->m_def   = "128.63.2.53";
	m++;

	m->m_title = "dns root name server 8";
	m->m_desc  = "IP address of a DNS root server. Assumes UDP "
		"port 53.";
	m->m_cgi   = "rnsh";
	m->m_off   = (char *)&g_conf.m_rnsIps[7] - g;
	m->m_type  = TYPE_IP;
	m->m_def   = "192.36.148.17";
	m++;

	m->m_title = "dns root name server 9";
	m->m_desc  = "IP address of a DNS root server. Assumes UDP "
		"port 53.";
	m->m_cgi   = "rnsi";
	m->m_off   = (char *)&g_conf.m_rnsIps[8] - g;
	m->m_type  = TYPE_IP;
	m->m_def   = "192.58.128.30";
	m++;

	m->m_title = "dns root name server 10";
	m->m_desc  = "IP address of a DNS root server. Assumes UDP "
		"port 53.";
	m->m_cgi   = "rnsj";
	m->m_off   = (char *)&g_conf.m_rnsIps[9] - g;
	m->m_type  = TYPE_IP;
	m->m_def   = "193.0.14.129";
	m++;

	m->m_title = "dns root name server 11";
	m->m_desc  = "IP address of a DNS root server. Assumes UDP "
		"port 53.";
	m->m_cgi   = "rnsk";
	m->m_off   = (char *)&g_conf.m_rnsIps[10] - g;
	m->m_type  = TYPE_IP;
	m->m_def   = "198.32.64.12";
	m++;

	m->m_title = "dns root name server 12";
	m->m_desc  = "IP address of a DNS root server. Assumes UDP "
		"port 53.";
	m->m_cgi   = "rnsl";
	m->m_off   = (char *)&g_conf.m_rnsIps[11] - g;
	m->m_type  = TYPE_IP;
	m->m_def   = "202.12.27.33";
	m++;

	m->m_title = "dns root name server 13";
	m->m_desc  = "IP address of a DNS root server. Assumes UDP "
		"port 53.";
	m->m_cgi   = "rnsm";
	m->m_off   = (char *)&g_conf.m_rnsIps[12] - g;
	m->m_type  = TYPE_IP;
	m->m_def   = "198.41.0.4";
	m++;
	*/

	m->m_title = "dns 0";
	m->m_desc  = "IP address of the primary DNS server. Assumes UDP "
		"port 53. REQUIRED FOR SPIDERING! Use Google's "
		"public DNS 8.8.8.8 as default.";
	m->m_cgi   = "pdns";
	m->m_off   = (char *)&g_conf.m_dnsIps[0] - g;
	m->m_type  = TYPE_IP;
	// default to google public dns #1
	m->m_def   = "8.8.8.8";
	m++;

	m->m_title = "dns 1";
	m->m_desc  = "IP address of the secondary DNS server. Assumes UDP "
	"port 53. Will be accessed in conjunction with the primary "
	"dns, so make sure this is always up. An ip of 0 means "
	"disabled. Google's secondary public DNS is 8.8.4.4.";
	m->m_cgi   = "sdns";
	m->m_off   = (char *)&g_conf.m_dnsIps[1] - g;
	m->m_type  = TYPE_IP;
	// default to google public dns #2
	m->m_def   = "8.8.4.4";
	m->m_group = 0;
	m++;

	m->m_title = "dns 2";
	m->m_desc  = "All hosts send to these DNSes based on hash "
		"of the subdomain to try to split DNS load evenly.";
	m->m_cgi   = "sdnsa";
	m->m_off   = (char *)&g_conf.m_dnsIps[2] - g;
	m->m_type  = TYPE_IP;
	m->m_def   = "0.0.0.0";
	m->m_group = 0;
	m++;

	m->m_title = "dns 3";
	m->m_desc  = "";
	m->m_cgi   = "sdnsb";
	m->m_off   = (char *)&g_conf.m_dnsIps[3] - g;
	m->m_type  = TYPE_IP;
	m->m_def   = "0.0.0.0";
	m->m_group = 0;
	m++;

	m->m_title = "dns 4";
	m->m_desc  = "";
	m->m_cgi   = "sdnsc";
	m->m_off   = (char *)&g_conf.m_dnsIps[4] - g;
	m->m_type  = TYPE_IP;
	m->m_def   = "0.0.0.0";
	m->m_group = 0;
	m++;

	m->m_title = "dns 5";
	m->m_desc  = "";
	m->m_cgi   = "sdnsd";
	m->m_off   = (char *)&g_conf.m_dnsIps[5] - g;
	m->m_type  = TYPE_IP;
	m->m_def   = "0.0.0.0";
	m->m_group = 0;
	m++;

	m->m_title = "dns 6";
	m->m_desc  = "";
	m->m_cgi   = "sdnse";
	m->m_off   = (char *)&g_conf.m_dnsIps[6] - g;
	m->m_type  = TYPE_IP;
	m->m_def   = "0.0.0.0";
	m->m_group = 0;
	m++;

	m->m_title = "dns 7";
	m->m_desc  = "";
	m->m_cgi   = "sdnsf";
	m->m_off   = (char *)&g_conf.m_dnsIps[7] - g;
	m->m_type  = TYPE_IP;
	m->m_def   = "0.0.0.0";
	m->m_group = 0;
	m++;

	m->m_title = "dns 8";
	m->m_desc  = "";
	m->m_cgi   = "sdnsg";
	m->m_off   = (char *)&g_conf.m_dnsIps[8] - g;
	m->m_type  = TYPE_IP;
	m->m_def   = "0.0.0.0";
	m->m_group = 0;
	m++;

	m->m_title = "dns 9";
	m->m_desc  = "";
	m->m_cgi   = "sdnsh";
	m->m_off   = (char *)&g_conf.m_dnsIps[9] - g;
	m->m_type  = TYPE_IP;
	m->m_def   = "0.0.0.0";
	m->m_group = 0;
	m++;

	m->m_title = "dns 10";
	m->m_desc  = "";
	m->m_cgi   = "sdnsi";
	m->m_off   = (char *)&g_conf.m_dnsIps[10] - g;
	m->m_type  = TYPE_IP;
	m->m_def   = "0.0.0.0";
	m->m_group = 0;
	m++;

	m->m_title = "dns 11";
	m->m_desc  = "";
	m->m_cgi   = "sdnsj";
	m->m_off   = (char *)&g_conf.m_dnsIps[11] - g;
	m->m_type  = TYPE_IP;
	m->m_def   = "0.0.0.0";
	m->m_group = 0;
	m++;

	m->m_title = "dns 12";
	m->m_desc  = "";
	m->m_cgi   = "sdnsk";
	m->m_off   = (char *)&g_conf.m_dnsIps[12] - g;
	m->m_type  = TYPE_IP;
	m->m_def   = "0.0.0.0";
	m->m_group = 0;
	m++;

	m->m_title = "dns 13";
	m->m_desc  = "";
	m->m_cgi   = "sdnsl";
	m->m_off   = (char *)&g_conf.m_dnsIps[13] - g;
	m->m_type  = TYPE_IP;
	m->m_def   = "0.0.0.0";
	m->m_group = 0;
	m++;

	m->m_title = "dns 14";
	m->m_desc  = "";
	m->m_cgi   = "sdnsm";
	m->m_off   = (char *)&g_conf.m_dnsIps[14] - g;
	m->m_type  = TYPE_IP;
	m->m_def   = "0.0.0.0";
	m->m_group = 0;
	m++;

	m->m_title = "dns 15";
	m->m_desc  = "";
	m->m_cgi   = "sdnsn";
	m->m_off   = (char *)&g_conf.m_dnsIps[15] - g;
	m->m_type  = TYPE_IP;
	m->m_def   = "0.0.0.0";
	m->m_group = 0;
	m++;


	m->m_title = "geocoder IP #1";
	m->m_desc  = "";
	m->m_cgi   = "gca";
	m->m_off   = (char *)&g_conf.m_geocoderIps[0] - g;
	m->m_type  = TYPE_IP;
	m->m_def   = "10.5.66.11"; // sp1
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "geocoder IP #2";
	m->m_desc  = "";
	m->m_cgi   = "gcb";
	m->m_off   = (char *)&g_conf.m_geocoderIps[1] - g;
	m->m_type  = TYPE_IP;
	m->m_def   = "0.0.0.0";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "geocoder IP #3";
	m->m_desc  = "";
	m->m_cgi   = "gcc";
	m->m_off   = (char *)&g_conf.m_geocoderIps[2] - g;
	m->m_type  = TYPE_IP;
	m->m_def   = "0.0.0.0";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "geocoder IP #4";
	m->m_desc  = "";
	m->m_cgi   = "gcd";
	m->m_off   = (char *)&g_conf.m_geocoderIps[3] - g;
	m->m_type  = TYPE_IP;
	m->m_def   = "0.0.0.0";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "wiki proxy ip";
	m->m_desc  = "Access the wiki coll through this proxy ip";
	m->m_cgi   = "wpi";
	m->m_off   = (char *)&g_conf.m_wikiProxyIp - g;
	m->m_type  = TYPE_IP;
	m->m_def   = "0";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;


	m->m_title = "wiki proxy port";
	m->m_desc  = "Access the wiki coll through this proxy port";
	m->m_cgi   = "wpp";
	m->m_off   = (char *)&g_conf.m_wikiProxyPort - g;
	m->m_type  = TYPE_LONG;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;


	m->m_title = "default collection";
	m->m_desc  = "When no collection is explicitly specified, assume "
		"this collection name.";
	m->m_cgi   = "dcn";
	m->m_off   = (char *)&g_conf.m_defaultColl - g;
	m->m_type  = TYPE_STRING;
	m->m_size  = MAX_COLL_LEN+1;
	m->m_def   = "";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "directory collection";
	m->m_desc  = "Collection to be used for directory searching and "
		"display of directory topic pages.";
	m->m_cgi   = "dircn";
	m->m_off   = (char *)&g_conf.m_dirColl - g;
	m->m_type  = TYPE_STRING;
	m->m_size  = MAX_COLL_LEN+1;
	m->m_def   = "main";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "directory hostname";
	m->m_desc  = "Hostname of the server providing the directory. "
		     "Leave empty to use this host.";
	m->m_cgi   = "dirhn";
	m->m_off   = (char *)&g_conf.m_dirHost - g;
	m->m_type  = TYPE_STRING;
	m->m_size  = MAX_URL_LEN;
	m->m_def   = "";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "max incoming bandwidth for spider";
	m->m_desc  = "Total incoming bandwidth used by all spiders should "
		"not exceed this many kilobits per second. ";
	m->m_cgi   = "mkbps";
	m->m_off   = (char *)&g_conf.m_maxIncomingKbps - g;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "999999.0";
	m->m_units = "Kbps";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "max 1-minute sliding-window loadavg";
	m->m_desc  = "Spiders will shed load when their host exceeds this "
		"value for the 1-minute load average in /proc/loadavg. "
		"The value 0.0 disables this feature.";
	m->m_cgi   = "mswl";
	m->m_off   = (char *)&g_conf.m_maxLoadAvg - g;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "0.0";
	m->m_units = "";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "max pages per second";
	m->m_desc  = "Maximum number of pages to index or delete from index "
		"per second for all hosts combined.";
	m->m_cgi   = "mpps";
	m->m_off   = (char *)&g_conf.m_maxPagesPerSecond - g;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "999999.0";
	m->m_units = "pages/second";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	/*
	m->m_title = "distributed spider balance";
	m->m_desc  = "Max number of ready domains a host can have distributed "
		"to it by all other host. This should be some multiple of the "
		"total number of hosts in the cluster.";
	m->m_cgi   = "dsb";
	m->m_off   = (char *)&g_conf.m_distributedSpiderBalance - g;
	m->m_type  = TYPE_LONG;
	m->m_def   = "1024";
	m->m_units = "domains";
	m++;

	m->m_title = "distributed same ip wait (hack)";
	m->m_desc  = "Amount of time to wait if this IP is already being "
		"downloaded by a host.  Works only in conjunction with "
		"distribute spider downloads by ip in Spider Controls.";
	m->m_cgi   = "dsiw";
	m->m_off   = (char *)&g_conf.m_distributedIpWait - g;
	m->m_type  = TYPE_LONG;
	m->m_def   = "0";
	m->m_units = "ms";
	m->m_group = 0;
	m->m_min   = 0;
	m++;
	*/


	/*
	m->m_title = "root quality max cache age base";
	m->m_desc  = "Maximum age to cache quality of a root url in seconds. "
		"Computing "
		"the quality of especially root urls can be expensive. "
		"This number is multiplied by (Q-30)/10 where Q is the cached "
		"quality of the root url. Therefore, higher quality and more "
		"stable root urls are updated less often, which is a good thing "
		"since they are more expensive to recompute.";
	m->m_cgi   = "rqmca";
	m->m_off   = (char *)&g_conf.m_siteQualityMaxCacheAge - g;
	m->m_type  = TYPE_LONG;
	m->m_def   = "7257600"; // 3 months (in seconds)
	m->m_units = "seconds";
	m++;
	*/

	m->m_title = "max cpu threads";
	m->m_desc  = "Maximum number of threads to use per Gigablast process "
		"for intersecting docid lists.";
	m->m_cgi   = "mct";
	m->m_off   = (char *)&g_conf.m_maxCpuThreads - g;
	m->m_type  = TYPE_LONG;
	// make it 3 for new gb in case one query takes way longer 
	// than the others
	m->m_def   = "6"; // "2";
	m->m_units = "threads";
	m->m_min   = 1;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "max cpu merge threads";
	m->m_desc  = "Maximum number of threads to use per Gigablast process "
		"for merging lists read from disk.";
	m->m_cgi   = "mcmt";
	m->m_off   = (char *)&g_conf.m_maxCpuMergeThreads - g;
	m->m_type  = TYPE_LONG;
	m->m_def   = "10";
	m->m_units = "threads";
	m->m_min   = 1;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "max write threads";
	m->m_desc  = "Maximum number of threads to use per Gigablast process "
		"for writing data to the disk. "
		"Keep low to reduce file interlace effects and impact "
		"on query response time.";
	m->m_cgi   = "mwt";
	m->m_off   = (char *)&g_conf.m_maxWriteThreads - g;
	m->m_type  = TYPE_LONG;
	m->m_def   = "1";
	m->m_units = "threads";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "do synchronous writes";
	m->m_desc  = "If enabled then all writes will be flushed to disk. "
		"This is generally a good thing.";
	m->m_cgi   = "fw";
	m->m_off   = (char *)&g_conf.m_flushWrites - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "max spider read threads";
	m->m_desc  = "Maximum number of threads to use per Gigablast process "
		"for accessing the disk "
		"for index-building purposes. Keep low to reduce impact "
		"on query response time. Increase for RAID systems or when "
		"initially building an index.";
	m->m_cgi   = "smdt";
	m->m_off   = (char *)&g_conf.m_spiderMaxDiskThreads - g;
	m->m_type  = TYPE_LONG;
	m->m_def   = "7";
	m->m_units = "threads";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "max spider big read threads";
	m->m_desc  = "This particular number applies to all reads above 1MB.";
	m->m_cgi   = "smbdt";
	m->m_off   = (char *)&g_conf.m_spiderMaxBigDiskThreads - g;
	m->m_type  = TYPE_LONG;
	m->m_def   = "3"; // 1
	m->m_units = "threads";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "max spider medium read threads";
	m->m_desc  = "This particular number applies to all reads above 100K.";
	m->m_cgi   = "smmdt";
	m->m_off   = (char *)&g_conf.m_spiderMaxMedDiskThreads - g;
	m->m_type  = TYPE_LONG;
	m->m_def   = "4"; // 3
	m->m_units = "threads";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "max spider small read threads";
	m->m_desc  = "This particular number applies to all reads above 1MB.";
	m->m_cgi   = "smsdt";
	m->m_off   = (char *)&g_conf.m_spiderMaxSmaDiskThreads - g;
	m->m_type  = TYPE_LONG;
	m->m_def   = "5";
	m->m_units = "threads";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "max query read threads";
	m->m_desc  = "Maximum number of threads to use per Gigablast process "
		"for accessing the disk "
		"for querying purposes. IDE systems tend to be more "
		"responsive when this is low. Increase for SCSI or RAID "
		"systems.";
	m->m_cgi   = "qmdt";
	m->m_off   = (char *)&g_conf.m_queryMaxDiskThreads - g;
	m->m_type  = TYPE_LONG;
	m->m_def   = "20";
	m->m_units = "threads";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "max query big read threads";
	m->m_desc  = "This particular number applies to all reads above 1MB.";
	m->m_cgi   = "qmbdt";
	m->m_off   = (char *)&g_conf.m_queryMaxBigDiskThreads - g;
	m->m_type  = TYPE_LONG;
	m->m_def   = "20"; // 1
	m->m_units = "threads";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "max query medium read threads";
	m->m_desc  = "This particular number applies to all reads above 100K.";
	m->m_cgi   = "qmmdt";
	m->m_off   = (char *)&g_conf.m_queryMaxMedDiskThreads - g;
	m->m_type  = TYPE_LONG;
	m->m_def   = "20"; // 3
	m->m_units = "threads";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "max query small read threads";
	m->m_desc  = "This particular number applies to all reads above 1MB.";
	m->m_cgi   = "qmsdt";
	m->m_off   = (char *)&g_conf.m_queryMaxSmaDiskThreads - g;
	m->m_type  = TYPE_LONG;
	m->m_def   = "20";
	m->m_units = "threads";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "min popularity for speller";
	m->m_desc  = "Word or phrase must be present in this percent "
		"of documents in order to qualify as a spelling "
		"recommendation.";
	m->m_cgi   = "mps";
	m->m_off   = (char *)&g_conf.m_minPopForSpeller - g;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = ".01";
	m->m_units = "%%";
	m->m_priv  = 2;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "phrase weight";
	m->m_desc  = "Percent to weight phrases in queries.";
	m->m_cgi   = "qp";
	m->m_off   = (char *)&g_conf.m_queryPhraseWeight - g;
	m->m_type  = TYPE_FLOAT;
	// was 350, but 'new mexico tourism' and 'boots uk'
	// emphasized the phrase terms too much!!
	m->m_def   = "100"; 
	m->m_units = "%%";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "weights.cpp slider parm (tmp)";
	m->m_desc  = "Percent of how much to use words to phrase ratio weights.";
	m->m_cgi   = "wsp";
	m->m_off   = (char *)&g_conf.m_sliderParm - g;
	m->m_type  = TYPE_LONG;
	m->m_def   = "90";
	m->m_units = "%%";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	/*
	m->m_title = "indextable intersection algo to use";
	m->m_desc  = "0 means adds the term scores, 1 means average them "
		"and 2 means take the RMS.";
	m->m_cgi   = "iia";
	m->m_off   = (char *)&g_conf.m_indexTableIntersectionAlgo - g;
	m->m_type  = TYPE_LONG;
	m->m_def   = "2";
	m->m_group = 0;
	m++;
	*/

	/*
	m->m_title = "max weight";
	m->m_desc  = "Maximum, relative query term weight. Set to 0 or less "
		"to indicate now max. 10.0 or 20.0 might be a good value.";
	m->m_cgi   = "qm";
	m->m_off   = (char *)&g_conf.m_queryMaxMultiplier - g;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "0.0";
	m->m_group = 0;
	m++;
	*/

	/*
	m->m_title = "query term exponent";
	m->m_desc  = "Raise the weights of the query "
		"terms to this power. The weight of a query term is "
		"basically the log of its term frequency. Increasing "
		"this will increase the effects of the term frequency "
		"related to each term in the query. Term frequency is "
		"also known as the term popularity. Very common words "
		"typically have lower weights tied to them, but the effects "
		"of such weighting will be increased if you increase this "
		"exponent.";
	m->m_cgi   = "qte";
	m->m_off   = (char *)&g_conf.m_queryExp - g;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "1.1";
	m->m_group = 0;
	m++;
	*/

	/*
	m->m_title = "use dynamic phrase weighting";
	m->m_desc  = "A new algorithm which reduces the weight on a query "
		"word term if the query phrase terms it is in are of "
		"similar popularity (term frequency) to that of the word "
		"term.";
	m->m_cgi   = "udpw";
	m->m_off   = (char *)&g_conf.m_useDynamicPhraseWeighting - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_group = 0;
	m++;
	*/

	m->m_title = "maximum serialized query size";
	m->m_desc  = "When passing queries around the network, send the raw "
		"string instead of the serialized query if the required "
		"buffer is bigger than this. Smaller values decrease network "
		"traffic for large queries at the expense of processing time.";
	m->m_cgi   = "msqs";
	m->m_off   = (char *)&g_conf.m_maxSerializedQuerySize - g;
	m->m_type  = TYPE_LONG;
	m->m_def   = "8192";
	m->m_units = "bytes";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "merge buf size";
	m->m_desc  = "Read and write this many bytes at a time when merging "
		"files.  Smaller values are kinder to query performance, "
		" but the merge takes longer. Use at least 1000000 for "
		"fast merging.";
	m->m_cgi   = "mbs";
	m->m_off   = (char *)&g_conf.m_mergeBufSize - g;
	m->m_type  = TYPE_LONG;
	// keep this way smaller than that 800k we had in here, 100k seems
	// to be way better performance for qps
	m->m_def   = "500000";
	m->m_units = "bytes";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "catdb minRecSizes";
	m->m_desc  = "minRecSizes for Catdb lookups";
	m->m_cgi   = "catmsr";
	m->m_off   = (char *)&g_conf.m_catdbMinRecSizes - g;
	m->m_type  = TYPE_LONG;
	m->m_def   = "100000000"; // 100 million
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	/*
	m->m_title = "max http download sockets";
	m->m_desc  = "Maximum sockets available to spiders for downloading "
		"web pages.";
	m->m_cgi   = "mds";
	m->m_off   = (char *)&g_conf.m_httpMaxDownloadSockets - g;
	m->m_type  = TYPE_LONG;
	m->m_def   = "5000";
	m->m_group = 0;
	m++;
	*/

	m->m_title = "doc count adjustment";
	m->m_desc  = "Add this number to the total document count in the "
		"index. Just used for displaying on the homepage.";
	m->m_cgi   = "dca";
	m->m_off   = (char *)&g_conf.m_docCountAdjustment - g;
	m->m_type  = TYPE_LONG;
	m->m_def   = "0";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "dynamic performance graph";
	m->m_desc  = "Generates profiling data for callbacks on page "
		"performance";
	m->m_cgi   = "dpg";
	m->m_off   = (char *)&g_conf.m_dynamicPerfGraph - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "enable profiling";
	m->m_desc  = "Enable profiler to do accounting of time taken by "
		"functions. ";
	m->m_cgi   = "enp";
	m->m_off   = (char *)&g_conf.m_profilingEnabled - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "minimum profiling threshold";
	m->m_desc  = "Profiler will not show functions which take less "
		"than this many milliseconds "
		"in the log or  on the perfomance graph.";
	m->m_cgi   = "mpt";
	m->m_off   = (char *)&g_conf.m_minProfThreshold - g;
	m->m_type  = TYPE_LONG;
	m->m_def   = "10";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;


	m->m_title = "sequential profiling.";
	m->m_desc  = "Produce a LOG_TIMING log message for each "
		"callback called, along with the time it took.  "
		"Profiler must be enabled.";
	m->m_cgi   = "ensp";
	m->m_off   = (char *)&g_conf.m_sequentialProfiling - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;
	
	m->m_title = "use statsdb";
	m->m_desc  = "Archive system statistics information in Statsdb.";
	m->m_cgi   = "usdb"; 
	m->m_off   = (char *)&g_conf.m_useStatsdb - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	/*
	m->m_title = "statsdb snapshots.";
	m->m_desc  = "Archive system statistics information in Statsdb. "
		     "Takes one snapshot every minute.";
	m->m_cgi   = "sdbss"; 
	m->m_off   = (char *)&g_conf.m_statsdbSnapshots - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_group = 0;
	m++;
	
	m->m_title = "statsdb web interface.";
	m->m_desc  = "Enable the Statsdb page for viewing stats history.";
	m->m_cgi   = "sdbwi"; 
	m->m_off   = (char *)&g_conf.m_statsdbPageEnabled - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_group = 0;
	m++;
	*/
	/*
	m->m_title = "max synonyms";
	m->m_desc = "Maximum possible synonyms to expand a word to.";
	m->m_cgi   = "msyn";
	m->m_off   = (char *)&g_conf.m_maxSynonyms - g;
	m->m_def   = "5";
	m->m_type  = TYPE_LONG;
	m++;

	m->m_title = "default affinity";
	m->m_desc = "spelling/number synonyms get this number as their "
		"affinity; negative values mean treat them as unknown, "
		"values higher than 1.0 get treated as 1.0";
	m->m_cgi   = "daff";
	m->m_off   = (char *)&g_conf.m_defaultAffinity - g;
	m->m_def   = "0.9";
	m->m_type  = TYPE_FLOAT;
	m++;

	m->m_title = "frequency threshold";
	m->m_desc = "the minimum amount a synonym term has to be in relation "
		"to its master term in order to be considered as a synonym";
	m->m_cgi   = "fqth";
	m->m_off   = (char *)&g_conf.m_frequencyThreshold - g;
	m->m_def   = "0.25";
	m->m_type  = TYPE_FLOAT;
	m++;

	m->m_title = "maximum affinity requests";
	m->m_desc = "Maximum number of outstanding requests the affinity "
		"builder can generate. Keep this number at 10 or lower for "
		"local servers, higher for internet servers or servers with "
		"high latency.";
	m->m_cgi   = "mar";
	m->m_off   = (char *)&g_conf.m_maxAffinityRequests - g;
	m->m_def   = "10";
	m->m_type  = TYPE_LONG;
	m->m_group = 0;
	m++;

	m->m_title = "maximum affinity errors";
	m->m_desc  = "Maximum number of times the affinity builder should "
		"encounter an error before giving up entirely.";
	m->m_cgi   = "mae";
	m->m_off   = (char *)&g_conf.m_maxAffinityErrors - g;
	m->m_def   = "100";
	m->m_type  = TYPE_LONG;
	m->m_group = 0;
	m++;

	m->m_title = "affinity timeout";
	m->m_desc = "Amount of time in milliseconds to wait for a response to "
		"an affinity query. You shouldn't have to touch this unless "
		"the network is slow or overloaded.";
	m->m_cgi   = "ato";
	m->m_off   = (char *)&g_conf.m_affinityTimeout - g;
	m->m_def   = "30000";
	m->m_type  = TYPE_LONG;
	m->m_group = 0;
	m++;

	m->m_title = "affinity rebuild server";
	m->m_desc = "Use this server:port to rebuild the affinity.";
	m->m_cgi   = "ars";
	m->m_off   = (char *)&g_conf.m_affinityServer - g;
	m->m_def   = "localhost:8000";
	m->m_type  = TYPE_STRING;
	m->m_size  = MAX_URL_LEN;
	m->m_group = 0;
	m++;

	m->m_title = "additional affinity parameters";
	m->m_desc = "Additional parameters to pass in the query. Tweak these "
		"to get better/faster responses. Don't touch the raw parameter "
		"unless you know what you are doing.";
	m->m_cgi  = "aap";
	m->m_off  = (char *)&g_conf.m_affinityParms - g;
	m->m_def  = "&raw=5&dio=1&n=1000&code=gbmonitor";
	m->m_type = TYPE_STRING;
	m->m_size = MAX_URL_LEN;
	m->m_group = 0;
	m++;
	*/


	///////////////////////////////////////////
	//  AUTOBAN CONTROLS
	//  
	///////////////////////////////////////////

	m->m_title = "ban IPs";
	m->m_desc  = "add Ips here to bar them from accessing this "
		"gigablast server.";
	m->m_cgi   = "banIps";
	m->m_xml   = "banIps";
	m->m_off   = (char *)g_conf.m_banIps - g;
	m->m_type  = TYPE_STRINGBOX;
	m->m_page  = PAGE_AUTOBAN;
	m->m_size  = AUTOBAN_TEXT_SIZE;
	m->m_group = 1;
	m->m_def   = "";
	m->m_sparm = 0;
	m->m_plen  = (char *)&g_conf.m_banIpsLen - g; // length of string
	m++;

	m->m_title = "allow IPs";
	m->m_desc  = "add Ips here to give them an infinite query quota.";
	m->m_cgi   = "allowIps";
	m->m_xml   = "allowIps";
	m->m_off   = (char *)g_conf.m_allowIps - g;
	m->m_type  = TYPE_STRINGBOX;
	m->m_page  = PAGE_AUTOBAN;
	m->m_size  = AUTOBAN_TEXT_SIZE;
	m->m_group = 1;
	m->m_sparm = 0;
	m->m_def   = "";
	m->m_plen  = (char *)&g_conf.m_allowIpsLen - g; // length of string
	m++;

	m->m_title = "valid search codes";
	m->m_desc  = "Don't try to autoban queries that have one "
		"of these codes. Also, the code must be valid for us "
		"to use &uip=IPADDRESS as the IP address of the submitter "
		"for purposes of autoban AND purposes of addurl daily quotas.";
	m->m_cgi   = "validCodes";
	m->m_xml   = "validCodes";
	m->m_off   = (char *)g_conf.m_validCodes - g;
	m->m_type  = TYPE_STRINGBOX;
	m->m_page  = PAGE_AUTOBAN;
	m->m_size  = AUTOBAN_TEXT_SIZE;
	m->m_group = 1;
	m->m_def   = "";
	m->m_sparm = 0;
	m->m_plen  = (char *)&g_conf.m_validCodesLen - g; // length of string
	m++;

	m->m_title = "Extra Parms";
	m->m_desc  = "Append extra default parms to queries that match "
		"certain substrings.  Format: text to match in url, "
		"followed by a space, then the list of extra parms as "
		"they would appear appended to the url.  "
		"One match per line.";
	m->m_cgi   = "extraParms";
	m->m_xml   = "extraParms";
	m->m_off   = (char *)g_conf.m_extraParms - g;
	m->m_type  = TYPE_STRINGBOX;
	m->m_page  = PAGE_AUTOBAN;
	m->m_size  = AUTOBAN_TEXT_SIZE;
	m->m_group = 1;
	m->m_def   = "";
	m->m_sparm = 0;
	m->m_plen  = (char *)&g_conf.m_extraParmsLen - g; // length of string
	m++;

	m->m_title = "ban substrings";
	m->m_desc  = "ban any query that matches this list of "
		"substrings.  Must match all comma-separated strings "
		"on the same line.  ('\\n' = OR, ',' = AND)";
	m->m_cgi   = "banRegex";
	m->m_xml   = "banRegex";
	m->m_off   = (char *)g_conf.m_banRegex - g;
	m->m_type  = TYPE_STRINGBOX;
	m->m_page  = PAGE_AUTOBAN;
	m->m_size  = AUTOBAN_TEXT_SIZE;
	m->m_group = 1;
	m->m_sparm = 0;
	m->m_def   = "";
	m->m_plen  = (char *)&g_conf.m_banRegexLen - g; // length of string
	m++;

	
	///////////////////////////////////////////
	// SECURITY CONTROLS
	///////////////////////////////////////////


	m->m_title = "Admin Passwords";
	m->m_desc  = "Passwords allowed to change Gigablast's general "
		"parameters and also the parameters for any collection. "
		"If no AdminPassword or Admin IP is specified then "
		"Gigablast will only allow local IPs to connect to it "
		"as the master admin.";
	m->m_cgi   = "mpwd";
	m->m_xml   = "masterPassword";
	m->m_max   = MAX_MASTER_PASSWORDS;
	m->m_off   = (char *)&g_conf.m_masterPwds - g;
	m->m_type  = TYPE_STRINGNONEMPTY;
	m->m_size  = PASSWORD_MAX_LEN+1;
	m->m_page  = PAGE_SECURITY;
	m++;


	m->m_title = "Admin IPs";
	//m->m_desc = "Allow UDP requests from this list of IPs. Any datagram "
	//	"received not coming from one of these IPs, or an IP in "
	//	"hosts.conf, is dropped. If another cluster is accessing this "
	//	"cluster for getting link text or whatever, you will need to "
	//	"list the IPs of the accessing machines here. These IPs are "
	//	"also used to allow access to the HTTP server even if it "
	//	"was disabled in the Master Controls. IPs that have 0 has "
	//	"their Least Significant Byte are treated as wildcards for "
	//	"IP blocks. That is, 1.2.3.0 means 1.2.3.*.";
	m->m_desc  = "Any IPs in this list will have administrative access "
		"to the Gigablast search engine.";
	m->m_cgi   = "adminip";
	m->m_xml   = "adminIp";
	m->m_page  = PAGE_SECURITY;
	m->m_max   = MAX_CONNECT_IPS;
	m->m_off   = (char *)g_conf.m_connectIps - g;
	m->m_type  = TYPE_IP;
	m->m_priv  = 2;
	m->m_def   = "";
	//m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "remove connect ip";
	m->m_desc  = "remove a connect ip";
	m->m_cgi   = "removeip";
	m->m_type  = TYPE_CMD;
	m->m_page  = PAGE_NONE;
	m->m_func  = CommandRemoveConnectIpRow;
	m->m_cast  = 1;
	m++;


	/*
	m->m_title = "Super Turks";
	m->m_desc = "Add facebook user IDs here so those people can "
		"turk the results. Later we may limit each person to "
		"turking a geographic region.";
	m->m_cgi = "supterturks";
	m->m_xml = "supterturks";
	m->m_def = "";
	m->m_off = (char *)&g_conf.m_superTurks - g;
	m->m_type = TYPE_STRINGBOX;
	m->m_perms = PAGE_MASTER;
	m->m_size = USERS_TEXT_SIZE;
	m->m_plen = (char *)&g_conf.m_superTurksLen - g;
	m->m_page = PAGE_SECURITY;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;
	*/

	/*
	m->m_title = "Users";
	m->m_desc = "Add users here. The format is "
		"collection:ip:username:password:relogin:pages:tagnames"
		" Username and password cannot be blank."
		" You can specify "
		"* for collection to indicate all collections. "
		" * can be used in IP as wildcard. "
		" * in pages means user has access to all pages. Also"
		" you can specify individual pages. A \'-\' sign at the"
		" start of page means user is not allowed to access that"
		" page. Please refer the page reference table at the bottom "
		"of this page for available pages. If you want to just login "
		" once and avoid relogin for gb shutdowns then set relogin=1,"
		" else set it to 0. If relogin is 1 your login will never expire either."
		"<br>"
		" Ex: 1. master user -> *:*:master:master:1:*:english<br>"
		" 2. public user -> *:*:public:1234:0:index.html"
		",get,search,login,dir:english<br>"
		"3. turk user ->  66.28.58.122:main:turk:1234:0:pageturkhome,"
		"pageturk,pageturkget,get,login:english";
	m->m_cgi = "users";
	m->m_xml = "users";
	m->m_off = (char *)&g_conf.m_users - g;
	m->m_type = TYPE_STRINGBOX;
	m->m_perms = PAGE_MASTER;
	m->m_size = USERS_TEXT_SIZE;
	m->m_plen = (char *)&g_conf.m_usersLen - g;
	m->m_page = PAGE_SECURITY;
	m++;
	*/

	/*
	m->m_title = "Master IPs";
	m->m_desc  = "If someone connects from one of these IPs "
		"then they will have full "
		"master administrator priviledges. "
		"If no IPs are specified, then master administrators can "
		"get access for any IP. "
		"Connecting from 127.0.0.1 always grants master privledges. "
		"If no Master Password or Master IP is specified then "
		"Gigablast will assign a default password of footbar23.";
	m->m_cgi   = "masterip";
	m->m_xml   = "masterIp";
	m->m_max   = MAX_MASTER_IPS;
	m->m_off   = (char *)g_conf.m_masterIps - g;
	m->m_type  = TYPE_IP;
	m++;
	*/

	///////////////////////////////////////////
	// LOG CONTROLS
	///////////////////////////////////////////

	m->m_title = "log http requests";
	m->m_desc  = "Log GET and POST requests received from the "
		"http server?";
	m->m_cgi   = "hr";
	m->m_off   = (char *)&g_conf.m_logHttpRequests - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_page  = PAGE_LOG;
	m++;

	m->m_title = "log autobanned queries";
	m->m_desc  = "Should we log queries that are autobanned? "
		"They can really fill up the log.";
	m->m_cgi   = "laq";
	m->m_off   = (char *)&g_conf.m_logAutobannedQueries - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m++;

	m->m_title = "log query time threshold";
	m->m_desc  = "If query took this many millliseconds or longer, then log the "
		"query and the time it took to process.";
	m->m_cgi   = "lqtt";
	m->m_off   = (char *)&g_conf.m_logQueryTimeThreshold- g;
	m->m_type  = TYPE_LONG;
	m->m_def   = "5000";
	m++;

	m->m_title = "log query reply";
	m->m_desc  = "Log query reply in proxy, but only for those queries "
		"above the time threshold above.";
	m->m_cgi   = "lqr";
	m->m_off   = (char *)&g_conf.m_logQueryReply - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_group = 0;
	m++;

	m->m_title = "log spidered urls";
	m->m_desc  = "Log status of spidered or injected urls?";
	m->m_cgi   = "lsu";
	m->m_off   = (char *)&g_conf.m_logSpideredUrls - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m++;

	m->m_title = "log network congestion";
	m->m_desc  = "Log messages if Gigablast runs out of udp sockets?";
	m->m_cgi   = "lnc";
	m->m_off   = (char *)&g_conf.m_logNetCongestion - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log informational messages";
	m->m_desc  = "Log messages not related to an error condition, "
		"but meant more to give an idea of the state of "
		"the gigablast process. These can be useful when "
		"diagnosing problems.";
	m->m_cgi   = "li";
	m->m_off   = (char *)&g_conf.m_logInfo - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m++;

	m->m_title = "log limit breeches";
	m->m_desc  = "Log it when document not added due to quota "
		"breech. Log it when url is too long and it gets "
		"truncated.";
	m->m_cgi   = "ll";
	m->m_off   = (char *)&g_conf.m_logLimits - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log debug admin messages";
	m->m_desc  = "Log various debug messages.";
	m->m_cgi   = "lda";
	m->m_off   = (char *)&g_conf.m_logDebugAdmin - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log debug build messages";
	m->m_cgi   = "ldb";
	m->m_off   = (char *)&g_conf.m_logDebugBuild - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log debug build time messages";
	m->m_cgi   = "ldbt";
	m->m_off   = (char *)&g_conf.m_logDebugBuildTime - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log debug database messages";
	m->m_cgi   = "ldd";
	m->m_off   = (char *)&g_conf.m_logDebugDb - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log debug dirty messages";
	m->m_cgi   = "lddm";
	m->m_off   = (char *)&g_conf.m_logDebugDirty - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log debug disk messages";
	m->m_cgi   = "lddi";
	m->m_off   = (char *)&g_conf.m_logDebugDisk - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log debug dns messages";
	m->m_cgi   = "lddns";
	m->m_off   = (char *)&g_conf.m_logDebugDns - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log debug http messages";
	m->m_cgi   = "ldh";
	m->m_off   = (char *)&g_conf.m_logDebugHttp - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log debug loop messages";
	m->m_cgi   = "ldl";
	m->m_off   = (char *)&g_conf.m_logDebugLoop - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log debug language detection messages";
	m->m_cgi   = "ldg";
	m->m_off   = (char *)&g_conf.m_logDebugLang - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log debug link info";
	m->m_cgi   = "ldli";
	m->m_off   = (char *)&g_conf.m_logDebugLinkInfo - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log debug mem messages";
	m->m_cgi   = "ldm";
	m->m_off   = (char *)&g_conf.m_logDebugMem - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log debug mem usage messages";
	m->m_cgi   = "ldmu";
	m->m_off   = (char *)&g_conf.m_logDebugMemUsage - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log debug net messages";
	m->m_cgi   = "ldn";
	m->m_off   = (char *)&g_conf.m_logDebugNet - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log debug post query rerank messages";
	m->m_cgi   = "ldpqr";
	m->m_off   = (char *)&g_conf.m_logDebugPQR - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "log debug query messages";
	m->m_cgi   = "ldq";
	m->m_off   = (char *)&g_conf.m_logDebugQuery - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log debug quota messages";
	m->m_cgi   = "ldqta";
	m->m_off   = (char *)&g_conf.m_logDebugQuota - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log debug robots messages";
	m->m_cgi   = "ldr";
	m->m_off   = (char *)&g_conf.m_logDebugRobots - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log debug spider cache messages";
	m->m_cgi   = "lds";
	m->m_off   = (char *)&g_conf.m_logDebugSpcache - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	/*
	m->m_title = "log debug spider wait messages";
	m->m_cgi   = "ldspw";
	m->m_off   = (char *)&g_conf.m_logDebugSpiderWait - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;
	*/

	m->m_title = "log debug speller messages";
	m->m_cgi   = "ldsp";
	m->m_off   = (char *)&g_conf.m_logDebugSpeller - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log debug sections messages";
	m->m_cgi   = "ldscc";
	m->m_off   = (char *)&g_conf.m_logDebugSections - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log debug seo insert messages";
	m->m_cgi   = "ldsi";
	m->m_off   = (char *)&g_conf.m_logDebugSEOInserts - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log debug seo messages";
	m->m_cgi   = "ldseo";
	m->m_off   = (char *)&g_conf.m_logDebugSEO - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log debug stats messages";
	m->m_cgi   = "ldst";
	m->m_off   = (char *)&g_conf.m_logDebugStats - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log debug summary messages";
	m->m_cgi   = "ldsu";
	m->m_off   = (char *)&g_conf.m_logDebugSummary - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log debug spider messages";
	m->m_cgi   = "ldspid";
	m->m_off   = (char *)&g_conf.m_logDebugSpider - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log debug url attempts";
	m->m_cgi   = "ldspua";
	m->m_off   = (char *)&g_conf.m_logDebugUrlAttempts - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log debug spider downloads";
	m->m_cgi   = "ldsd";
	m->m_off   = (char *)&g_conf.m_logDebugDownloads - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log debug facebook";
	m->m_cgi   = "ldfb";
	m->m_off   = (char *)&g_conf.m_logDebugFacebook - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log debug tagdb messages";
	m->m_cgi   = "ldtm";
	m->m_off   = (char *)&g_conf.m_logDebugTagdb - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log debug tcp messages";
	m->m_cgi   = "ldt";
	m->m_off   = (char *)&g_conf.m_logDebugTcp - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log debug thread messages";
	m->m_cgi   = "ldth";
	m->m_off   = (char *)&g_conf.m_logDebugThread - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log debug title messages";
	m->m_cgi   = "ldti";
	m->m_off   = (char *)&g_conf.m_logDebugTitle - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log debug timedb messages";
	m->m_cgi   = "ldtim";
	m->m_off   = (char *)&g_conf.m_logDebugTimedb - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log debug topic messages";
	m->m_cgi   = "ldto";
	m->m_off   = (char *)&g_conf.m_logDebugTopics - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log debug topDoc messages";
	m->m_cgi   = "ldtopd";
	m->m_off   = (char *)&g_conf.m_logDebugTopDocs - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log debug udp messages";
	m->m_cgi   = "ldu";
	m->m_off   = (char *)&g_conf.m_logDebugUdp - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log debug unicode messages";
	m->m_cgi   = "ldun";
	m->m_off   = (char *)&g_conf.m_logDebugUnicode - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log debug repair messages";
	m->m_cgi   = "ldre";
	m->m_off   = (char *)&g_conf.m_logDebugRepair - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log debug pub date extraction messages";
	m->m_cgi   = "ldpd";
	m->m_off   = (char *)&g_conf.m_logDebugDate - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log timing messages for build";
	m->m_desc  = "Log various timing related messages.";
	m->m_cgi   = "ltb";
	m->m_off   = (char *)&g_conf.m_logTimingBuild - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log timing messages for admin";
	m->m_desc  = "Log various timing related messages.";
	m->m_cgi   = "ltadm";
	m->m_off   = (char *)&g_conf.m_logTimingAdmin - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log timing messages for database";
	m->m_cgi   = "ltd";
	m->m_off   = (char *)&g_conf.m_logTimingDb - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log timing messages for network layer";
	m->m_cgi   = "ltn";
	m->m_off   = (char *)&g_conf.m_logTimingNet - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log timing messages for query";
	m->m_cgi   = "ltq";
	m->m_off   = (char *)&g_conf.m_logTimingQuery - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log timing messages for spcache";
	m->m_desc  = "Log various timing related messages.";
	m->m_cgi   = "ltspc";
	m->m_off   = (char *)&g_conf.m_logTimingSpcache - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log timing messages for related topics";
	m->m_cgi   = "ltt";
	m->m_off   = (char *)&g_conf.m_logTimingTopics - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	m->m_title = "log reminder messages";
	m->m_desc  = "Log reminders to the programmer. You do not need this.";
	m->m_cgi   = "lr";
	m->m_off   = (char *)&g_conf.m_logReminders - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 1;
	m++;

	///////////////////////////////////////////
	// SYNC CONTROLS
	///////////////////////////////////////////
	/*

	m->m_title = "sync enabled";
	m->m_desc  = "Turn data synchronization on or off. When a host comes "
		"up he will perform an incremental synchronization with a "
		"twin if he detects that he was unable to save his data "
		"when he last exited.";
	m->m_cgi   = "sye";
	m->m_off   = (char *)&g_conf.m_syncEnabled - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_page  = PAGE_SYNC;
	m++;

	m->m_title = "dry run";
	m->m_desc  = "Should Gigablast just run through and log the changes "
		"it would make without actually making them?";
	m->m_cgi   = "sdr";
	m->m_off   = (char *)&g_conf.m_syncDryRun - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m++;

	m->m_title = "sync indexdb";
	m->m_desc  = "Turn data synchronization on or off for indexdb. "
		"Indexdb holds the index information.";
	m->m_cgi   = "si";
	m->m_off   = (char *)&g_conf.m_syncIndexdb - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m++;

	m->m_title = "sync logging";
	m->m_desc  = "Log fixes?";
	m->m_cgi   = "slf";
	m->m_off   = (char *)&g_conf.m_syncLogging - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m++;

	m->m_title = "union titledb and spiderdb";
	m->m_desc  = "If a host being sync'd has a title record (cached web "
		"page) that the "
		"remote host does not, normally, it would be deleted. "
		"But if this is true then it is kept. "
		"Useful for reducing title rec not found errors.";
	m->m_cgi   = "sdu";
	m->m_off   = (char *)&g_conf.m_syncDoUnion - g;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m++;

	m->m_title = "force out of sync";
	m->m_desc  = "Forces this host to be out of sync.";
	m->m_cgi   = "foos";
	m->m_type  = TYPE_CMD;
	m->m_func  = CommandForceOutOfSync;
	m->m_cast  = 0;
	m++;

	m->m_title = "bytes per second";
	m->m_desc  = "How many bytes to read per second for syncing. "
		  "Decrease to reduce impact of syncing on query "
		"response time.";
	m->m_cgi   = "sbps";
	m->m_off   = (char *)&g_conf.m_syncBytesPerSecond - g;
	m->m_type  = TYPE_LONG;
	m->m_def   = "10000000";
	m->m_units = "bytes";
	m++;
	*/

	/////////////////////
	//
	// DIFFBOT CRAWLBOT PARMS
	//
	//////////////////////

	///////////
	//
	// DO NOT INSERT parms above here, unless you set
	// m_obj = OBJ_COLL !!! otherwise it thinks it belongs to
	// OBJ_CONF as used in the above parms.
	//
	///////////

	m->m_cgi   = "dbtoken";
	m->m_xml   = "diffbotToken";
	m->m_off   = (char *)&cr.m_diffbotToken - x;
	m->m_type  = TYPE_SAFEBUF;
	m->m_page  = PAGE_NONE;
	m->m_obj   = OBJ_COLL;
	m->m_def   = "";
	m->m_flags = PF_DIFFBOT;
	m++;

	m->m_cgi   = "dbcrawlname";
	m->m_xml   = "diffbotCrawlName";
	m->m_off   = (char *)&cr.m_diffbotCrawlName - x;
	m->m_type  = TYPE_SAFEBUF;
	m->m_obj   = OBJ_COLL;
	m->m_def   = "";
	m->m_flags = PF_DIFFBOT;
	m++;

	m->m_cgi   = "notifyEmail";
	m->m_title = "notify email";
	m->m_xml   = "notifyEmail";
	m->m_off   = (char *)&cr.m_notifyEmail - x;
	m->m_type  = TYPE_SAFEBUF;
	m->m_page  = PAGE_NONE;
	m->m_obj   = OBJ_COLL;
	m->m_def   = "";
	m->m_flags = PF_DIFFBOT;
	m++;

	m->m_cgi   = "notifyWebhook";
	m->m_xml   = "notifyWebhook";
	m->m_title = "notify webhook";
	m->m_off   = (char *)&cr.m_notifyUrl - x;
	m->m_type  = TYPE_SAFEBUF;
	m->m_page  = PAGE_NONE;
	m->m_obj   = OBJ_COLL;
	m->m_def   = "";
	m->m_flags = PF_DIFFBOT;
	m++;

	// collective respider frequency (for pagecrawlbot.cpp)
	m->m_title = "collective respider frequency (days)";
	m->m_cgi   = "repeat";
	m->m_xml   = "collectiveRespiderFrequency";
	m->m_off   = (char *)&cr.m_collectiveRespiderFrequency - x;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "0.0"; // 0.0
	m->m_page  = PAGE_NONE;
	m->m_units = "days";
	m->m_flags = PF_REBUILDURLFILTERS | PF_DIFFBOT;
	m++;

	m->m_title = "collective crawl delay (seconds)";
	m->m_cgi   = "crawlDelay";
	m->m_xml   = "collectiveCrawlDelay";
	m->m_off   = (char *)&cr.m_collectiveCrawlDelay - x;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = ".250"; // 250 ms
	m->m_page  = PAGE_NONE;
	m->m_flags = PF_REBUILDURLFILTERS | PF_DIFFBOT;
	m->m_units = "seconds";
	m++;

	m->m_cgi   = "urlCrawlPattern";
	m->m_xml   = "diffbotUrlCrawlPattern";
	m->m_title = "url crawl pattern";
	m->m_off   = (char *)&cr.m_diffbotUrlCrawlPattern - x;
	m->m_type  = TYPE_SAFEBUF;
	m->m_page  = PAGE_NONE;
	m->m_def   = "";
	m->m_flags = PF_REBUILDURLFILTERS | PF_DIFFBOT;
	m++;

	m->m_cgi   = "urlProcessPattern";
	m->m_xml   = "diffbotUrlProcessPattern";
	m->m_title = "url process pattern";
	m->m_off   = (char *)&cr.m_diffbotUrlProcessPattern - x;
	m->m_type  = TYPE_SAFEBUF;
	m->m_page  = PAGE_NONE;
	m->m_def   = "";
	m->m_flags = PF_REBUILDURLFILTERS | PF_DIFFBOT;
	m++;

	m->m_cgi   = "pageProcessPattern";
	m->m_xml   = "diffbotPageProcessPattern";
	m->m_title = "page process pattern";
	m->m_off   = (char *)&cr.m_diffbotPageProcessPattern - x;
	m->m_type  = TYPE_SAFEBUF;
	m->m_page  = PAGE_NONE;
	m->m_def   = "";
	m->m_flags = PF_REBUILDURLFILTERS | PF_DIFFBOT;
	m++;

	m->m_cgi   = "urlCrawlRegEx";
	m->m_xml   = "diffbotUrlCrawlRegEx";
	m->m_title = "url crawl regex";
	m->m_off   = (char *)&cr.m_diffbotUrlCrawlRegEx - x;
	m->m_type  = TYPE_SAFEBUF;
	m->m_page  = PAGE_NONE;
	m->m_def   = "";
	m->m_flags = PF_REBUILDURLFILTERS | PF_DIFFBOT;
	m++;

	m->m_cgi   = "urlProcessRegEx";
	m->m_xml   = "diffbotUrlProcessRegEx";
	m->m_title = "url process regex";
	m->m_off   = (char *)&cr.m_diffbotUrlProcessRegEx - x;
	m->m_type  = TYPE_SAFEBUF;
	m->m_page  = PAGE_NONE;
	m->m_def   = "";
	m->m_flags = PF_REBUILDURLFILTERS | PF_DIFFBOT;
	m++;

	m->m_cgi   = "onlyProcessIfNew";
	m->m_xml   = "diffbotOnlyProcessIfNew";
	m->m_title = "onlyProcessIfNew";
	m->m_off   = (char *)&cr.m_diffbotOnlyProcessIfNew - x;
	m->m_type  = TYPE_BOOL;
	m->m_page  = PAGE_NONE;
	m->m_def   = "1";
	m->m_flags = PF_REBUILDURLFILTERS | PF_DIFFBOT;
	m++;

	m->m_cgi   = "seeds";
	m->m_xml   = "diffbotSeeds";
	m->m_off   = (char *)&cr.m_diffbotSeeds - x;
	m->m_type  = TYPE_SAFEBUF;
	m->m_page  = PAGE_NONE;
	m->m_obj   = OBJ_COLL;
	m->m_flags = PF_DIFFBOT;
	m->m_def   = "";
	m++;

	m->m_xml   = "isCustomCrawl";
	m->m_off   = (char *)&cr.m_isCustomCrawl - x;
	m->m_type  = TYPE_CHAR;
	m->m_page  = PAGE_NONE;
	m->m_cgi   = "isCustomCrawl";
	m->m_def   = "0";
	m->m_flags = PF_DIFFBOT;
	m++;

	m->m_cgi   = "maxToCrawl";
	m->m_title = "max to crawl";
	m->m_xml   = "maxToCrawl";
	m->m_off   = (char *)&cr.m_maxToCrawl - x;
	m->m_type  = TYPE_LONG_LONG;
	m->m_page  = PAGE_NONE;
	m->m_def   = "100000";
	m->m_flags = PF_DIFFBOT;
	m++;

	m->m_cgi   = "maxToProcess";
	m->m_title = "max to process";
	m->m_xml   = "maxToProcess";
	m->m_off   = (char *)&cr.m_maxToProcess - x;
	m->m_type  = TYPE_LONG_LONG;
	m->m_page  = PAGE_NONE;
	m->m_def   = "-1";
	m->m_flags = PF_DIFFBOT;
	m++;

	m->m_cgi   = "maxRounds";
	m->m_title = "max crawl rounds";
	m->m_xml   = "maxCrawlRounds";
	m->m_off   = (char *)&cr.m_maxCrawlRounds - x;
	m->m_type  = TYPE_LONG;
	m->m_page  = PAGE_NONE;
	m->m_def   = "-1";
	m->m_flags = PF_DIFFBOT;
	m++;

	/////////////////////
	//
	// new cmd parms
	//
	/////////////////////


	m->m_title = "insert parm row";
	m->m_desc  = "insert a row into a parm";
	m->m_cgi   = "insert";
	m->m_type  = TYPE_CMD;
	m->m_page  = PAGE_NONE;
	m->m_func  = CommandInsertUrlFiltersRow;
	m->m_cast  = 1;
	m->m_flags = PF_REBUILDURLFILTERS;
	m++;

	m->m_title = "remove parm row";
	m->m_desc  = "remove a row from a parm";
	m->m_cgi   = "remove";
	m->m_type  = TYPE_CMD;
	m->m_page  = PAGE_NONE;
	m->m_func  = CommandRemoveUrlFiltersRow;
	m->m_cast  = 1;
	m->m_flags = PF_REBUILDURLFILTERS;
	m++;

	m->m_title = "delete collection";
	m->m_desc  = "delete a collection";
	m->m_cgi   = "delete";
	m->m_type  = TYPE_CMD;
	m->m_page  = PAGE_NONE;
	m->m_func2 = CommandDeleteColl;
	m->m_cast  = 1;
	m++;

	m->m_title = "delete collection 2";
	m->m_desc  = "delete the specified collection";
	m->m_cgi   = "delColl";
	m->m_type  = TYPE_CMD;
	m->m_page  = PAGE_NONE;
	m->m_func2 = CommandDeleteColl2;
	m->m_cast  = 1;
	m++;

	m->m_title = "add collection";
	m->m_desc  = "add a new collection";
	m->m_cgi   = "addColl";
	m->m_type  = TYPE_CMD;
	m->m_page  = PAGE_NONE;
	m->m_func  = CommandAddColl0;
	m->m_cast  = 1;
	m++;

	m->m_title = "add custom crawl";
	m->m_desc  = "add custom crawl";
	m->m_cgi   = "addCrawl";
	m->m_type  = TYPE_CMD;
	m->m_page  = PAGE_NONE;
	m->m_func  = CommandAddColl1;
	m->m_cast  = 1;
	m++;

	m->m_title = "add bulk job";
	m->m_desc  = "add bulk job";
	m->m_cgi   = "addBulk";
	m->m_type  = TYPE_CMD;
	m->m_page  = PAGE_NONE;
	m->m_func  = CommandAddColl2;
	m->m_cast  = 1;
	m++;

	m->m_title = "in sync";
	m->m_desc  = "signify in sync with host 0";
	m->m_cgi   = "insync";
	m->m_type  = TYPE_CMD;
	m->m_page  = PAGE_NONE;
	m->m_func  = CommandInSync;
	m->m_cast  = 1;
	m++;


	///////////////////////////////////////////
	// SPIDER CONTROLS
	///////////////////////////////////////////

	// just a comment in the conf file
	m->m_desc  = 
		"All <, >, \" and # characters that are values for a field "
		"contained herein must be represented as "
		"&lt;, &gt;, &#34; and &#035; respectively.";
	m->m_type  = TYPE_COMMENT;
	m->m_page  = PAGE_SPIDER;
	m->m_obj   = OBJ_COLL;
	m++;

	m->m_title = "spidering enabled";
	m->m_desc  = "Controls just the spiders for this collection.";
	m->m_cgi   = "cse";
	m->m_off   = (char *)&cr.m_spideringEnabled - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m++;

	m->m_title = "reset collection";
	m->m_desc  = "Remove all documents from the collection and turn "
		"spiders off.";
	m->m_cgi   = "reset";
	m->m_type  = TYPE_CMD;
	m->m_page  = PAGE_SPIDER;
	m->m_func2 = CommandResetColl;
	m->m_cast  = 1;
	m++;

	m->m_title = "restart collection";
	m->m_desc  = "Remove all documents from the collection and start "
		"spidering over again.";
	m->m_cgi   = "restart";
	m->m_type  = TYPE_CMD;
	m->m_page  = PAGE_SPIDER;
	m->m_func2 = CommandRestartColl;
	m->m_cast  = 1;
	m++;

	/*
	m->m_title = "new spidering enabled";
	m->m_desc  = "When enabled the spider adds NEW "
		"pages to your index. ";
	m->m_cgi  = "nse";
	m->m_off   = (char *)&cr.m_newSpideringEnabled - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m++;

	m->m_title = "old spidering enabled";
	m->m_desc  = "When enabled the spider will re-visit "
		"and update pages that are already in your index.";
	m->m_cgi  = "ose";
	m->m_off   = (char *)&cr.m_oldSpideringEnabled - x;
	m->m_type  = TYPE_BOOL; 
	m->m_def   = "0";
	m->m_group = 0;
	m++;

	m->m_title = "new spider weight";
	m->m_desc  = "Weight time slices of new spiders in the priority "
		"page by this factor relative to the old spider queues.";
	m->m_cgi  = "nsw";
	m->m_off   = (char *)&cr.m_newSpiderWeight - x;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "1.0";
	m->m_group = 0;
	m++;
	*/

	m->m_title = "max spiders";
	m->m_desc  = "What is the maximum number of web "
		"pages the spider is allowed to download "
		"simultaneously PER HOST for THIS collection?";
	m->m_cgi   = "mns";
	m->m_off   = (char *)&cr.m_maxNumSpiders - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "100";
	m++;

	m->m_title = "spider delay in milliseconds";
	m->m_desc  = "make each spider wait this many milliseconds before "
		"getting the ip and downloading the page.";
	m->m_cgi  = "sdms";
	m->m_off   = (char *)&cr.m_spiderDelayInMilliseconds - x; 
	m->m_type  = TYPE_LONG;
	m->m_def   = "0";
	m->m_group = 0;
	m++; 


	m->m_title = "use robots.txt";
	m->m_desc  = "If this is true Gigablast will respect "
		"the robots.txt convention.";
	m->m_cgi   = "obeyRobots";
	m->m_off   = (char *)&cr.m_useRobotsTxt - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m++;

	m->m_title = "max robots.txt cache age";
	m->m_desc  = "How many second to cache a robots.txt file for. "
		"86400 is 1 day. 0 means Gigablast will not read from the "
		"cache at all and will download the robots.txt before every "
		"page if robots.txt use is enabled above. However, if this is "
		"0 then Gigablast will still store robots.txt files into the "
		"cache.";
	m->m_cgi   = "mrca";
	m->m_off   = (char *)&cr.m_maxRobotsCacheAge - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "86400"; // 24*60*60 = 1day
	m->m_units = "seconds";
	m->m_group = 0;
	m++;




	/*
	m->m_title = "add url enabled";
	m->m_desc  = "If this is enabled others can add "
		"web pages to your index via the add url page.";
	m->m_cgi   = "aue";
	m->m_off   = (char *)&cr.m_addUrlEnabled - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m++;
	*/

	m->m_title = "daily merge time";
	m->m_desc  = "Do a tight merge on posdb and titledb at this time "
		"every day. This is expressed in MINUTES past midnight UTC. "
		"UTC is 5 hours ahead "
		"of EST and 7 hours ahead of MST. Leave this as -1 to "
		"NOT perform a daily merge. To merge at midnight EST use "
		"60*5=300 and midnight MST use 60*7=420.";
	m->m_cgi   = "dmt";
	m->m_off   = (char *)&cr.m_dailyMergeTrigger - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "-1";
	m->m_units = "minutes";
	m++;

	m->m_title = "daily merge days";
	m->m_desc  = "Comma separated list of days to merge on. Use "
		"0 for Sunday, 1 for Monday, ... 6 for Saturday. Leaving "
		"this parmaeter empty or without any numbers will make the "
		"daily merge happen every day";
	m->m_cgi   = "dmdl";
	m->m_off   = (char *)&cr.m_dailyMergeDOWList - x;
	m->m_type  = TYPE_STRING;
	m->m_size  = 48;
	// make sunday the default
	m->m_def   = "0";
	m->m_group = 0;
	m++;

	m->m_title = "daily merge last started";
	m->m_desc  = "When the daily merge was last kicked off. Expressed in "
		"UTC in seconds since the epoch.";
	m->m_cgi   = "dmls";
	m->m_off   = (char *)&cr.m_dailyMergeStarted - x;
	m->m_type  = TYPE_LONG_CONST;
	m->m_def   = "-1";
	m->m_group = 0;
	m++;

	/*
	m->m_title = "use datedb";
	m->m_desc  = "Index documents for generating results sorted by date "
		"or constrained by date range. Only documents indexed while "
		"this is enabled will be returned for date-related searches.";
	m->m_cgi   = "ud";
	m->m_off   = (char *)&cr.m_useDatedb - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m++;

	m->m_title = "age cutoff for datedb";
	m->m_desc  = "Do not index pubdates into datedb that are more "
		"than this many days old. Use -1 for no limit. A value "
		"of zero essentially turns off datedb. Pre-existing pubdates "
		"in datedb that fail to meet this constraint WILL BE "
		"COMPLETELY ERASED when datedb is merged.";
	m->m_cgi   = "dbc";
	m->m_off   = (char *)&cr.m_datedbCutoff - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "-1";
	m->m_units = "days";
	m++;

	m->m_title = "datedb default timezone";
	m->m_desc  = "Default timezone to use when none specified on parsed "
		"time.  Use offset from GMT, i.e 0400 (AMT) or -0700 (MST)";
	m->m_cgi   = "ddbdt";
	m->m_off   = (char *)&cr.m_datedbDefaultTimezone - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "0";
	m->m_group = 0;
	m++;
	*/

	//m->m_title = "days before now to index";
	//m->m_desc  = "Only index page if the datedb date was found to be "
	//	"within this many days of the current time.  Use 0 to index "
	//	"all dates.  Parm is float for fine control.";
	//m->m_cgi   = "ddbdbn";
	//m->m_off   = (char *)&cr.m_datedbDaysBeforeNow - x;
	//m->m_type  = TYPE_FLOAT;
	//m->m_def   = "0";
	//m->m_group = 0;
	//m++;

	m->m_title = "turing test enabled";
	m->m_desc  = "If this is true, users will have to "
		"pass a simple Turing test to add a url. This prevents "
		"automated url submission.";
	m->m_cgi   = "dtt";
	m->m_off   = (char *)&cr.m_doTuringTest - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m++;

	m->m_title = "max add urls";
	m->m_desc = "Maximum number of urls that can be "
		"submitted via the addurl interface, per IP domain, per "
		"24 hour period. A value less than or equal to zero "
		"implies no limit.";
	m->m_cgi = "mau";
	m->m_off = (char *)&cr.m_maxAddUrlsPerIpDomPerDay - x;
	m->m_type = TYPE_LONG;
	m->m_def = "0";
	m->m_group = 0;
	m++;


	// use url filters harvest links parm for this now
	/*
	m->m_title = "spider links";
	m->m_desc  = "If this is false, the spider will not "
		"harvest links from web pages it visits. Links that it does "
		"harvest will be attempted to be indexed at a later time. ";
	m->m_cgi   = "sl";
	m->m_off   = (char *)&cr.m_spiderLinks - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m++;
	*/

	/*

	  MDW: use the "onsite" directive in the url filters page now...

	m->m_title = "only spider links from same host";
	m->m_desc  = "If this is true the spider will only harvest links "
		"to pages that are contained on the same host as the page "
		"that is being spidered. "
		"Example: When spidering a page from "
		"www.gigablast.com, only links to pages that are from "
		"www.gigablast.com would "
		"be harvested, if this switch were enabled. This allows you "
		"to seed the spider with URLs from a specific set of hosts "
		"and ensure that only links to pages that are from those "
		"hosts are harvested.";
	m->m_cgi   = "slsh";
	m->m_off   = (char *)&cr.m_sameHostLinks - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_group = 0;
	m++;
	*/

	m->m_title = "do not re-add old outlinks more than this many days";
	m->m_desc  = "If less than this many days have elapsed since the "
		"last time we added the outlinks to spiderdb, do not re-add "
		"them to spiderdb. Saves resources.";
	m->m_cgi   = "slrf";
	m->m_off   = (char *)&cr.m_outlinksRecycleFrequencyDays - x;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "30";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	/*
	m->m_title = "spider links by priority";
	m->m_desc   = "Specify priorities for which links should be spidered. "
		"If the <i>spider links</i> option above is "
		"disabled then these setting will have no effect.";
	m->m_cgi   = "slp";
	m->m_xml   = "spiderLinksByPriority";
	m->m_off   = (char *)&cr.m_spiderLinksByPriority - x;
	m->m_type  = TYPE_PRIORITY_BOXES; // array of numbered (0-(MAX_SPIDER_PRIORITIES-1)) checkboxes
	m->m_fixed = MAX_SPIDER_PRIORITIES;
	m->m_def   = "1"; // default for each one is on
	m->m_group = 0;
	m++;
	*/

	/*
	m->m_title = "min link priority";
	m->m_desc  = "Only add links to the spider "
		"queue if their spider priority is this or higher. "
		"This can make the spider process more efficient "
		"since a lot of disk seeks are used when adding "
		"links.";
	m->m_cgi   = "mlp";
	m->m_off   = (char *)&cr.m_minLinkPriority - x;
	m->m_type  = TYPE_PRIORITY;
	m->m_def   = "0";
	m->m_group = 0;
	m++;
	*/

	/*	m->m_title = "maximum hops from parent page";
	m->m_desc  = "Only index pages that are within a particular number "
		"of hops from the parent page given in Page Add Url. -1 means "
		"that max hops is infinite.";
	m->m_cgi   = "mnh";
	m->m_off   = (char *)&cr.m_maxNumHops - x;
	m->m_type  = TYPE_CHAR2;
	m->m_def   = "-1";
	m->m_group = 0;
	m++;*/

	m->m_title = "spider round start time";
	m->m_desc  = "When the spider round started";
	m->m_cgi   = "spiderRoundStart";
	m->m_off   = (char *)&cr.m_spiderRoundStartTime - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_REBUILDURLFILTERS ;
	m++;

	m->m_title = "spider round num";
	m->m_desc  = "The spider round number.";
	m->m_cgi   = "spiderRoundNum";
	m->m_off   = (char *)&cr.m_spiderRoundNum - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN ;
	m++;

	m->m_title = "scraping enabled procog";
	m->m_desc  = "Do searches for queries in this hosts part of the "
		"query log.";
	m->m_cgi   = "scrapepc";
	m->m_off   = (char *)&cr.m_scrapingEnabledProCog - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "scraping enabled web";
	m->m_desc  = "Perform random searches on googles news search engine "
		"to add sites with ingoogle tags into tagdb.";
	m->m_cgi   = "scrapeweb";
	m->m_off   = (char *)&cr.m_scrapingEnabledWeb - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "scraping enabled news";
	m->m_desc  = "Perform random searches on googles news search engine "
		"to add sites with news and goognews and ingoogle "
		"tags into tagdb.";
	m->m_cgi   = "scrapenews";
	m->m_off   = (char *)&cr.m_scrapingEnabledNews - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "scraping enabled blogs";
	m->m_desc  = "Perform random searches on googles news search engine "
		"to add sites with blogs and googblogs and ingoogle "
		"tags into tagdb.";
	m->m_cgi   = "scrapeblogs";
	m->m_off   = (char *)&cr.m_scrapingEnabledBlogs - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	/*
	m->m_title = "subsite detection enabled";
	m->m_desc  = "Add the \"sitepathdepth\" to tagdb if a hostname "
		"is determined to have subsites at a particular depth.";
	m->m_cgi   = "ssd";
	m->m_off   = (char *)&cr.m_subsiteDetectionEnabled - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m++;
	*/

	m->m_title = "deduping enabled";
	m->m_desc  = "When enabled, the spider will "
		"discard web pages which are identical to other web pages "
		"that are already in the index. "//AND that are from the same "
		//"hostname. 
		//"An example of a hostname is www1.ibm.com. "
		"However, root urls, urls that have no path, are never "
		"discarded. It most likely has to hit disk to do these "
		"checks so it does cause some slow down. Only use it if you "
		"need it.";
	m->m_cgi   = "de";
	m->m_off   = (char *)&cr.m_dedupingEnabled - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m++;

	m->m_title = "deduping enabled for www";
	m->m_desc  = "When enabled, the spider will "
		"discard web pages which, when a www is prepended to the "
		"page's url, result in a url already in the index.";
	m->m_cgi   = "dew";
	m->m_off   = (char *)&cr.m_dupCheckWWW - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_group = 0;
	m++;

	m->m_title = "detect custom error pages";
	m->m_desc  = "Detect and do not index pages which have a 200 status"
		" code, but are likely to be error pages.";
	m->m_cgi   = "dcep";
	m->m_off   = (char *)&cr.m_detectCustomErrorPages - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m++;

	m->m_title = "delete 404s";
	m->m_desc  = "Should pages be removed from the index if they are no "
		"longer accessible on the web?";
	m->m_cgi   = "dnf";
	m->m_off   = (char *)&cr.m_delete404s - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m++;

	m->m_title = "delete timed out docs";
	m->m_desc  = "Should documents be deleted from the index "
		"if they have been retried them enough times and the "
		"last received error is a time out? "
		"If your internet connection is flaky you may say "
		"no here to ensure you do not lose important docs.";
	m->m_cgi   = "dt";
	m->m_off   = (char *)&cr.m_deleteTimeouts - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "use simplified redirects";
	m->m_desc  = "If this is true, the spider, when a url redirects "
		"to a \"simpler\" url, will add that simpler url into "
		"the spider queue and abandon the spidering of the current "
		"url.";
	m->m_cgi   = "usr";
	m->m_off   = (char *)&cr.m_useSimplifiedRedirects - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m++;

	m->m_title = "use ifModifiedSince";
	m->m_desc  = "If this is true, the spider, when "
		"updating a web page that is already in the index, will "
		"not even download the whole page if it hasn't been "
		"updated since the last time Gigablast spidered it. "
		"This is primarily a bandwidth saving feature. It relies on "
		"the remote webserver's returned Last-Modified-Since field "
		"being accurate.";
	m->m_cgi   = "uims";
	m->m_off   = (char *)&cr.m_useIfModifiedSince - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m++;

	m->m_title = "build similarity vector from content only";
	m->m_desc  = "If this is true, the spider, when checking the page "
		     "if it has changed enough to reindex or update the "
		     "published date, it will build the vector only from "
		     "the content located on that page.";
	m->m_cgi   = "bvfc";
	m->m_off   = (char *)&cr.m_buildVecFromCont - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "use content similarity to index publish date";
	m->m_desc  = "This requires build similarity from content only to be "
		     "on.  This indexes the publish date (only if the content "
		     "has changed enough) to be between the last two spider "
		     "dates.";
	m->m_cgi   = "uspd";
	m->m_off   = (char *)&cr.m_useSimilarityPublishDate - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "max percentage similar to update publish date";
	m->m_desc  = "This requires build similarity from content only and "
		     "use content similarity to index publish date to be "
		     "on.  This percentage is the maximum similarity that can "
		     "exist between an old document and new before the publish "
		     "date will be updated.";
	m->m_cgi   = "mpspd";
	m->m_off   = (char *)&cr.m_maxPercentSimilarPublishDate - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "80";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	// use url filters for this. this is a crawlbot parm really.
	m->m_title = "restrict domain";
	m->m_desc  = "Keep crawler on same domain as seed urls?";
	m->m_cgi   = "restrictDomain";
	m->m_off   = (char *)&cr.m_restrictDomain - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	// we need to save this it is a diffbot parm
	m->m_flags = PF_HIDDEN | PF_DIFFBOT;// | PF_NOSAVE;
	m++;

	m->m_title = "do url sporn checking";
	m->m_desc  = "If this is true and the spider finds "
		"lewd words in the hostname of a url it will throw "
		"that url away. It will also throw away urls that have 5 or "
		"more hyphens in their hostname.";
	m->m_cgi   = "dusc";
	m->m_off   = (char *)&cr.m_doUrlSpamCheck - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	/*
	m->m_title = "hours before adding unspiderable url to spiderdb";
	m->m_desc  = "Hours to wait after trying to add an unspiderable url "
		"to spiderdb again.";
	m->m_cgi   = "dwma";
	m->m_off   = (char *)&cr.m_deadWaitMaxAge - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "24";
	m++;
	*/

	//m->m_title = "link text anomaly threshold";
	//m->m_desc  = "Prevent pages from link voting for "
	//	"another page if its link text has a "
	//	"word which doesn't occur in at least this "
	//	"many other link texts. (set to 1 to disable)";
	//m->m_cgi   = "ltat";
	//m->m_off   = (char *)&cr.m_linkTextAnomalyThresh - x;
	//m->m_type  = TYPE_LONG;
	//m->m_def   = "2";
	//m++;

	/*
	m->m_title = "enforce domain quotas on new docs";
	m->m_desc  = "If this is true then new documents will be removed "
		"from the index if the quota for their domain "
		"has been breeched.";
	m->m_cgi   = "enq";
	m->m_off   = (char *)&cr.m_enforceNewQuotas - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m++;

	m->m_title = "enforce domain quotas on indexed docs";
	m->m_desc  = "If this is true then indexed documents will be removed "
		"from the index if the quota for their domain has been "
		"breeched.";
	m->m_cgi   = "eoq";
	m->m_off   = (char *)&cr.m_enforceOldQuotas - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_group = 0;
	m++;

	m->m_title = "use exact quotas";
	m->m_desc  = "Does not use approximations so will do more disk seeks "
		"and may impact indexing performance significantly.";
	m->m_cgi   = "ueq";
	m->m_off   = (char *)&cr.m_exactQuotas - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_group = 0;
	m++;

	m->m_title = "restrict indexdb for spidering";
	m->m_desc  = "If this is true then only the root indexb file is "
		"searched for linkers. Saves on disk seeks, "
		"but may use older versions of indexed web pages.";
	m->m_cgi   = "ris";
	m->m_off   = (char *)&cr.m_restrictIndexdbForSpider - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m++;
	*/

	/*
	m->m_title = "indexdb max total files to merge";
	m->m_desc  = "Do not merge more than this many files during a single "
		"merge operation. Merge does not scale well to numbers above "
		"50 or so.";
	m->m_cgi   = "mttftm";
	m->m_off   = (char *)&cr.m_indexdbMinTotalFilesToMerge - x;
	m->m_def   = "50"; 
	//m->m_max   = 100;
	m->m_type  = TYPE_LONG;
	m++;

	m->m_title = "indexdb min files needed to trigger merge";
	m->m_desc  = "Merge is triggered when this many indexdb data files "
		"are on disk.";
	m->m_cgi   = "miftm";
	m->m_off   = (char *)&cr.m_indexdbMinFilesToMerge - x;
	m->m_def   = "6"; // default to high query performance, not spider
	m->m_type  = TYPE_LONG;
	m->m_group = 0;
	m++;

	m->m_title = "datedb min files needed to trigger to merge";
	m->m_desc  = "Merge is triggered when this many datedb data files "
		"are on disk.";
	m->m_cgi   = "mdftm";
	m->m_off   = (char *)&cr.m_datedbMinFilesToMerge - x;
	m->m_def   = "5";
	m->m_type  = TYPE_LONG;
	m->m_group = 0;
	m++;

	m->m_title = "spiderdb min files needed to trigger to merge";
	m->m_desc  = "Merge is triggered when this many spiderdb data files "
		"are on disk.";
	m->m_cgi   = "msftm";
	m->m_off   = (char *)&cr.m_spiderdbMinFilesToMerge - x;
	m->m_def   = "2";
	m->m_type  = TYPE_LONG;
	m->m_group = 0;
	m++;

	m->m_title = "checksumdb min files needed to trigger to merge";
	m->m_desc  = "Merge is triggered when this many checksumdb data files "
		"are on disk.";
	m->m_cgi   = "mcftm";
	m->m_off   = (char *)&cr.m_checksumdbMinFilesToMerge - x;
	m->m_def   = "2";
	m->m_type  = TYPE_LONG;
	m->m_group = 0;
	m++;

	m->m_title = "clusterdb min files needed to trigger to merge";
	m->m_desc  = "Merge is triggered when this many clusterdb data files "
		"are on disk.";
	m->m_cgi   = "mclftm";
	m->m_off   = (char *)&cr.m_clusterdbMinFilesToMerge - x;
	m->m_def   = "2";
	m->m_type  = TYPE_LONG;
	m->m_group = 0;
	m++;

	m->m_title = "linkdb min files needed to trigger to merge";
	m->m_desc  = "Merge is triggered when this many linkdb data files "
		"are on disk.";
	m->m_cgi   = "mlkftm";
	m->m_off   = (char *)&cr.m_linkdbMinFilesToMerge - x;
	m->m_def   = "4";
	m->m_type  = TYPE_LONG;
	m->m_group = 0;
	m++;
	*/

	//m->m_title = "tagdb min files to merge";
	//m->m_desc  = "Merge is triggered when this many linkdb data files "
	//	"are on disk.";
	//m->m_cgi   = "mtftm";
	//m->m_off   = (char *)&cr.m_tagdbMinFilesToMerge - x;
	//m->m_def   = "2"; 
	//m->m_type  = TYPE_LONG;
	//m->m_group = 0;
	//m++;

	// this is overridden by collection
	m->m_title = "titledb min files needed to trigger to merge";
	m->m_desc  = "Merge is triggered when this many titledb data files "
		"are on disk.";
	m->m_cgi   = "mtftm";
	m->m_off   = (char *)&cr.m_titledbMinFilesToMerge - x;
	m->m_def   = "6"; 
	m->m_type  = TYPE_LONG;
	//m->m_save  = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	//m->m_title = "sectiondb min files to merge";
	//m->m_desc  ="Merge is triggered when this many sectiondb data files "
	//	"are on disk.";
	//m->m_cgi   = "mscftm";
	//m->m_off   = (char *)&cr.m_sectiondbMinFilesToMerge - x;
	//m->m_def   = "4"; 
	//m->m_type  = TYPE_LONG;
	//m->m_group = 0;
	//m++;

	m->m_title = "posdb min files needed to trigger to merge";
	m->m_desc  = "Merge is triggered when this many posdb data files "
		"are on disk.";
	m->m_cgi   = "mpftm";
	m->m_off   = (char *)&cr.m_posdbMinFilesToMerge - x;
	m->m_def   = "6"; 
	m->m_type  = TYPE_LONG;
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "recycle content";
	m->m_desc   = "Rather than downloading the content again when "
		"indexing old urls, use the stored content. Useful for "
		"reindexing documents under a different ruleset or for "
		"rebuilding an index. You usually "
		"should turn off the 'use robots.txt' switch. "
		"And turn on the 'use old ips' and "
		"'recycle link votes' switches for speed. If rebuilding an "
		"index then you should turn off the 'only index changes' "
		"switches.";
	m->m_cgi   = "rc";
	m->m_off   = (char *)&cr.m_recycleContent - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "enable link voting";
	m->m_desc  = "If this is true Gigablast will "
		"index hyper-link text and use hyper-link "
		"structures to boost the quality of indexed documents.";
	m->m_cgi   = "glt";
	m->m_off   = (char *)&cr.m_getLinkInfo - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "do link spam checking";
	m->m_desc  = "If this is true, do not allow spammy inlinks to vote. "
		"This check is "
		"too aggressive for some collections, i.e.  it "
		"does not allow pages with cgi in their urls to vote.";
	m->m_cgi   = "dlsc";
	m->m_off   = (char *)&cr.m_doLinkSpamCheck - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m++;

	m->m_title = "restrict link voting by ip";
	m->m_desc  = "If this is true Gigablast will "
		"only allow one vote per the top 2 significant bytes "
		"of the IP address. Otherwise, multiple pages "
		"from the same top IP can contribute to the link text and "
		"link-based quality ratings of a particular URL. "
		"Furthermore, no votes will be accepted from IPs that have "
		"the same top 2 significant bytes as the IP of the page "
		"being indexed.";
	m->m_cgi   = "ovpid";
	m->m_off   = (char *)&cr.m_oneVotePerIpDom - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_group = 0;
	m++;

	m->m_title = "use new link algo";
	m->m_desc  = "Use the links: termlists instead of link:. Also "
		"allows pages linking from the same domain or IP to all "
		"count as a single link from a different IP. This is also "
		"required for incorporating RSS and Atom feed information "
		"when indexing a document.";
	m->m_cgi   = "na";
	m->m_off   = (char *)&cr.m_newAlgo - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	/*
	m->m_title = "recycle link votes";
	m->m_desc   = "If this is true Gigablast will "
		"use the old links and link text when re-indexing old urls "
		"and not do any link voting when indexing new urls.";
	m->m_cgi   = "rv";
	m->m_off   = (char *)&cr.m_recycleVotes - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_group = 0;
	m++;
	*/

	m->m_title = "update link info frequency";
	m->m_desc   = "How often should Gigablast recompute the "
		"link info for a url. "
		"Also applies to getting the quality of a site "
		"or root url, which is based on the link info. "
		"In days. Can use decimals. 0 means to update "
		"the link info every time the url's content is re-indexed. "
		"If the content is not reindexed because it is unchanged "
		"then the link info will not be updated. When getting the "
		"link info or quality of the root url from an "
		"external cluster, Gigablast will tell the external cluster "
		"to recompute it if its age is this or higher.";
	m->m_cgi   = "uvf";
	m->m_off   = (char *)&cr.m_updateVotesFreq - x;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "60.000000";
	m->m_group = 0;
	m++;

	/*
	m->m_title = "recycle imported link info";
	m->m_desc  = "If true, we ALWAYS recycle the imported link info and "
		"NEVER recompute it again. Otherwise, recompute it when we "
		"recompute the local link info.";
	m->m_cgi   = "rili";
	m->m_off   = (char *)&cr.m_recycleLinkInfo2 - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_group = 0;
	m++;
	*/

	/*
	m->m_title = "use imported link info for quality";
	m->m_desc  = "If true, we will use the imported link info to "
		"help us determine the quality of the page we are indexing.";
	m->m_cgi   = "uifq";
	m->m_off   = (char *)&cr.m_useLinkInfo2ForQuality - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_group = 0;
	m++;
	*/

	// this can hurt us too much if mis-assigned, remove it
	/*
	m->m_title = "restrict link voting to roots";
	m->m_desc  = "If this is true Gigablast will "
		"not perform link analysis on urls that are not "
		"root urls.";
	m->m_cgi   = "rvr";
	m->m_off   = (char *)&cr.m_restrictVotesToRoots - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_group = 0;
	m++;
	*/

	/*
	m->m_title = "index link text";
	m->m_desc  = "If this is true Gigablast will "
		"index both incoming and outgoing link text for the "
		"appropriate documents, depending on url filters and "
		"site rules, under the gbinlinktext: and gboutlinktext: "
		"fields. Generally, you want this disabled, it was for "
		"a client.";
	m->m_cgi   = "ilt";
	m->m_off   = (char *)&cr.m_indexLinkText - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_group = 0;
	m++;
	*/

	/*
	m->m_title = "index incoming link text";
	m->m_desc  = "If this is false no incoming link text is indexed.";
	m->m_cgi   = "iilt";
	m->m_off   = (char *)&cr.m_indexLinkText - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_group = 0;
	m++;
	*/

	m->m_title = "index inlink neighborhoods";
	m->m_desc  = "If this is true Gigablast will "
		"index the plain text surrounding the hyper-link text. The "
		"score will be x times that of the hyper-link text, where x "
		"is the scalar below.";
	m->m_cgi   = "iin";
	m->m_off   = (char *)&cr.m_indexInlinkNeighborhoods - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	/*
	// this is now hard-coded in XmlNode.cpp, currently .8
	m->m_title = "inlink neighborhoods score scalar";
	m->m_desc  = "Gigablast can "
		"index the plain text surrounding the hyper-link text. The "
		"score will be x times that of the hyper-link text, where x "
		"is this number.";
	m->m_cgi   = "inss";
	m->m_off   = (char *)&cr.m_inlinkNeighborhoodsScoreScalar - x;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = ".20";
	m->m_group = 0;
	m++;
	*/

	/*
	m->m_title = "break web rings";
	m->m_desc  = "If this is true Gigablast will "
		"attempt to detect link spamming rings and decrease "
		"their influence on the link text for a URL.";
	m->m_cgi   = "bwr";
	m->m_off   = (char *)&cr.m_breakWebRings - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_group = 0;
	m++;
	*/

	/*
	m->m_title = "break log spam";
	m->m_desc  = "If this is true Gigablast will attempt to detect "
		"dynamically generated pages and remove their voting power. "
		"Additionally, pages over 100k will not be have their "
		"outgoing links counted. Pages that have a form which POSTS "
		"to a cgi page will not be considered either.";
	m->m_cgi   = "bls";
	m->m_off   = (char *)&cr.m_breakLogSpam - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m++;
	*/

	m->m_title = "tagdb collection name";
	m->m_desc  = "Sometimes you want the spiders to use the tagdb of "
		"another collection, like the <i>main</i> collection. "
		"If this is empty it defaults to the current collection.";
	m->m_cgi   = "tdbc";
	m->m_off   = (char *)&cr.m_tagdbColl - x;
	m->m_type  = TYPE_STRING;
	m->m_size  = MAX_COLL_LEN+1;
	m->m_def   = "";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "catdb lookups enabled";
	m->m_desc  = "Spiders will look to see if the current page is in "
		"catdb.  If it is, all Directory information for that page "
		"will be indexed with it.";
	m->m_cgi   = "cdbe";
	m->m_off   = (char *)&cr.m_catdbEnabled - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "recycle catdb info";
	m->m_desc   = "Rather than requesting new info from DMOZ, like "
		"titles and topic ids, grab it from old record. Increases "
		"performance if you are seeing a lot of "
		"\"getting catdb record\" entries in the spider queues.";
	m->m_cgi   = "rci";
	m->m_off   = (char *)&cr.m_recycleCatdb - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "allow banning of pages in catdb";
	m->m_desc  = "If this is 'NO' then pages that are in catdb, "
		"but banned from tagdb or the url filters page, can not "
		"be banned.";
	m->m_cgi   = "abpc";
	m->m_off   = (char *)&cr.m_catdbPagesCanBeBanned - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "override spider errors for catdb";
	m->m_desc  = "Ignore and skip spider errors if the spidered site"
		     " is found in Catdb (DMOZ).";
	m->m_cgi   = "catose";
	m->m_off   = (char *)&cr.m_overrideSpiderErrorsForCatdb - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	//m->m_title = "only spider root urls";
	//m->m_desc  = "Only spider urls that are roots.";
	//m->m_cgi   = "osru";
	//m->m_off   = (char *)&cr.m_onlySpiderRoots - x;
	//m->m_type  = TYPE_BOOL;
	//m->m_def   = "0";
	//m++;

	m->m_title = "allow asian docs";
	m->m_desc  = "If this is disabled the spider "
		"will not allow any docs from the gb2312 charset "
		"into the index.";
	m->m_cgi   = "aad";
	m->m_off   = (char *)&cr.m_allowAsianDocs - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "allow adult docs";
	m->m_desc  = "If this is disabled the spider "
		"will not allow any docs which contain adult content "
		"into the index (overides tagdb).";
	m->m_cgi   = "aprnd";
	m->m_off   = (char *)&cr.m_allowAdultDocs - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_group =  0 ;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "allow xml docs";
	m->m_desc  = "If this is disabled the spider "
		"will not allow any xml "
		"into the index.";
	m->m_cgi   = "axd";
	m->m_off   = (char *)&cr.m_allowXmlDocs - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "do serp detection";
	m->m_desc  = "If this is eabled the spider "
		"will not allow any docs which are determined to "
		"be serps.";
	m->m_cgi   = "dsd";
	m->m_off   = (char *)&cr.m_doSerpDetection - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m++;


	m->m_title = "do IP lookup";
	m->m_desc  = "If this is disabled and the proxy "
		"IP below is not zero then Gigablast will assume "
		"all spidered URLs have an IP address of 1.2.3.4.";
	m->m_cgi   = "dil";
	m->m_off   = (char *)&cr.m_doIpLookups - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "use old IPs";
	m->m_desc = "Should the stored IP "
		"of documents we are reindexing be used? Useful for "
		"pages banned by IP address and then reindexed with "
		"the reindexer tool.";
	m->m_cgi = "useOldIps";
	m->m_off = (char *)&cr.m_useOldIps - x;
	m->m_type = TYPE_BOOL;
	m->m_def = "0";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "remove banned pages";
	m->m_desc  = "Remove banned pages from the index. Pages can be "
		"banned using tagdb or the Url Filters table.";
	m->m_cgi   = "rbp";
	m->m_off   = (char *)&cr.m_removeBannedPages - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	/*
	m->m_title = "ban domains of urls banned by IP";
	m->m_desc = "Most urls are banned by IP "
		"address. But owners often will keep the same "
		"domains and change their IP address. So when "
		"banning a url that was banned by IP, should its domain "
		"be banned too? (obsolete)";
	m->m_cgi = "banDomains";
	m->m_off = (char *)&cr.m_banDomains - x;
	m->m_type = TYPE_BOOL;
	m->m_def = "0";
	m++;
	*/

	m->m_title = "allow HTTPS pages using SSL";
	m->m_desc  = "If this is true, spiders will read "
		     "HTTPS pages using SSL Protocols.";
	m->m_cgi   = "ahttps";
	m->m_off   = (char *)&cr.m_allowHttps - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	/*
	m->m_title = "require dollar sign";
	m->m_desc  = "If this is YES, then do not allow document to be "
		"indexed if they do not contain a dollar sign ($), but the "
		"links will still be harvested. Used for building shopping "
		"index.";
	m->m_cgi   = "nds";
	m->m_off   = (char *)&cr.m_needDollarSign - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m++;
	*/

	/*
	m->m_title = "require numbers in url";
	m->m_desc  = "If this is YES, then do not allow document to be "
		"indexed if they do not have two back-to-back digits in the "
		"path of the url, but the links will still be harvested. Used "
		"to build a news index.";
	m->m_cgi   = "nniu";
	m->m_off   = (char *)&cr.m_needNumbersInUrl - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_group = 0;
	m++;

	m->m_title = "index news topics";
	m->m_desc  = "If this is YES, Gigablast will attempt to categorize "
		"every page as being in particular news categories like "
		"sports, business, etc. and will be searchable by doing a "
		"query like \"newstopic:sports.";
	m->m_cgi   = "int";
	m->m_off   = (char *)&cr.m_getNewsTopic - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m++;
	*/

	m->m_title = "follow RSS links";
	m->m_desc  = "If an item on a page has an RSS feed link, add the "
		"RSS link to the spider queue and index the RSS pages "
		"instead of the current page.";
	m->m_cgi   = "frss";
	m->m_off   = (char *)&cr.m_followRSSLinks - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "only index articles from RSS feeds";
	m->m_desc  = "Only index pages that were linked to by an RSS feed. "
		"Follow RSS Links must be enabled (above).";
	m->m_cgi   = "orss";
	m->m_off   = (char *)&cr.m_onlyIndexRSS - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	/*
	m->m_title = "max text doc length";
	m->m_desc  = "Gigablast will not download, index or "
		"store more than this many bytes of an html or text "
		"document. Use -1 for no max.";
	m->m_cgi   = "mtdl";
	m->m_off   = (char *)&cr.m_maxTextDocLen - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "204800";
	m++;

	m->m_title = "max other doc length";
	m->m_desc  = "Gigablast will not download, index or "
		"store more than this many bytes of a non-html, non-text "
		"document. Use -1 for no max.";
	m->m_cgi   = "modl";
	m->m_off   = (char *)&cr.m_maxOtherDocLen - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "1048576";
	m->m_group = 0;
	m++;
	*/

	//m->m_title = "indexdb truncation limit";
	//m->m_cgi   = "itl";
	//m->m_desc  = "How many documents per term? Keep this very high.";
	//m->m_off   = (char *)&cr.m_indexdbTruncationLimit - x;
	//m->m_def   = "50000000"; 
	//m->m_type  = TYPE_LONG;
	//m->m_min   = MIN_TRUNC; // from Indexdb.h
	//m++;

	m->m_title = "apply filter to text pages";
	m->m_desc  = "If this is false then the filter "
		"will not be used on html or text pages.";
	m->m_cgi   = "aft";
	m->m_off   = (char *)&cr.m_applyFilterToText - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m++;

	m->m_title = "filter name";
	m->m_desc  = "Program to spawn to filter all HTTP "
		"replies the spider receives. Leave blank for none.";
	m->m_cgi   = "filter";
	m->m_def   = "";
	m->m_off   = (char *)&cr.m_filter - x;
	m->m_type  = TYPE_STRING;
	m->m_size  = MAX_FILTER_LEN+1;
	m->m_group = 0;
	m++;

	m->m_title = "filter timeout";
	m->m_desc  = "Kill filter shell after this many seconds. Assume it "
		"stalled permanently.";
	m->m_cgi   = "fto";
	m->m_def   = "40";
	m->m_off   = (char *)&cr.m_filterTimeout - x;
	m->m_type  = TYPE_LONG;
	m->m_group = 0;
	m++;

	m->m_title = "proxy ip";
	m->m_desc  = "Retrieve pages from the proxy at this IP address.";
	m->m_cgi   = "proxyip";
	m->m_off   = (char *)&cr.m_proxyIp - x;
	m->m_type  = TYPE_IP;
	m->m_def   = "0.0.0.0";
	m++;

	m->m_title = "proxy port";
	m->m_desc  = "Retrieve pages from the proxy on "
		"this port.";
	m->m_cgi   = "proxyport";
	m->m_off   = (char *)&cr.m_proxyPort - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "0";
	m->m_group = 0;
	m++;

	// i put this in here so i can save disk space for my global
	// diffbot json index
	m->m_title = "index body";
	m->m_desc  = "Index the body of the documents so you can search it. "
		"Required for searching that. You wil pretty much always "
		"want to keep this enabled.";
	m->m_cgi   = "ib";
	m->m_off   = (char *)&cr.m_indexBody - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m++;

	m->m_cgi   = "apiUrl";
	m->m_desc  = "Send every spidered url to this diffbot.com "
		"by appending a "
		"&url=<url> to it before trinyg to downloading it. We "
		"expect get get back a JSON reply which we index. You will "
		"need to supply your token to this as well.";
	m->m_xml   = "diffbotApiUrl";
	m->m_title = "diffbot api url";
	m->m_off   = (char *)&cr.m_diffbotApiUrl - x;
	m->m_type  = TYPE_SAFEBUF;
	m->m_page  = PAGE_SPIDER;
	m->m_flags = PF_REBUILDURLFILTERS;
	m->m_def   = "";
	m++;


	m->m_title = "spider start time";
	m->m_desc  = "Only spider URLs scheduled to be spidered "
		"at this time or after. In UTC.";
	m->m_cgi   = "sta";
	m->m_off   = (char *)&cr.m_spiderTimeMin - x;
	m->m_type  = TYPE_DATE; // date format -- very special
	m->m_def   = "01 Jan 1970";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "spider end time";
	m->m_desc  = "Only spider URLs scheduled to be spidered "
		"at this time or before. If \"use current time\" is true "
		"then the current local time is used for this value instead. "
		"in UTC.";
	m->m_cgi   = "stb";
	m->m_off   = (char *)&cr.m_spiderTimeMax - x;
	m->m_type  = TYPE_DATE2;
	m->m_def   = "01 Jan 2010";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "use current time";
	m->m_desc  = "Use the current time as the spider end time?";
	m->m_cgi   = "uct";
	m->m_off   = (char *)&cr.m_useCurrentTime - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	/*
	m->m_title = "default ruleset site file num";
	m->m_desc  = "Use this as the current Sitedb file num for Sitedb "
		"entries that always use the current default";
	m->m_cgi   = "dftsfn";
	m->m_off   = (char *)&cr.m_defaultSiteRec - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "16";
	m++;

	m->m_title = "RSS ruleset site file num";
	m->m_desc  = "Use this Sitedb file num ruleset for RSS feeds";
	m->m_cgi   = "rssrs";
	m->m_off   = (char *)&cr.m_rssSiteRec - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "25";
	m->m_group = 0;
	m++;

	m->m_title = "TOC ruleset site file num";
	m->m_desc  = "Use this Sitedb file num ruleset "
		"for Table of Contents pages";
	m->m_cgi   = "tocrs";
	m->m_off   = (char *)&cr.m_tocSiteRec - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "29";
	m->m_group = 0;
	m++;
	*/

	/*
	m->m_title = "store topics vector";
	m->m_desc  = "Should Gigablast compute and store a topics vector "
		"for every document indexed. This allows Gigablast to "
		"do topic clustering without having to compute this vector "
		"at query time. You can turn topic clustering on in the "
		"Search Controls page.";
	m->m_cgi   = "utv";
	m->m_off   = (char *)&cr.m_useGigabitVector - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m++;

	m->m_title = "use gigabits for vector";
	m->m_desc  = "For news collection. "
		"Should Gigablast form the similarity vector using "
		"Gigabits, as opposed to a straight out random sample. "
		"This does clustering more "
		"by topic rather than by explicit content in common.";
	m->m_cgi   = "uct";
	m->m_off   = (char *)&cr.m_useGigabitVector - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m++;

	m->m_title = "max similarity to reindex";
	m->m_desc  = "If the url's content is over X% similar to what we "
		"already "
		"have indexed, then do not reindex it, and treat the content "
		"as if it were unchanged for intelligent spider scheduling "
		"purposes. Set to 100% to always reindex the document, "
		"regardless, although the use-ifModifiedSince check "
		"above may still be in affect, as well as the "
		"deduping-enabled check. This will also affect the re-spider "
		"time, because Gigablast spiders documents that change "
		"frequently faster.";
	m->m_cgi   = "msti";
	m->m_off   = (char *)&cr.m_maxSimilarityToIndex - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "100";
	m->m_group = 0;
	m++;
	*/

	// this is obsolete -- we can use the reg exp "isroot"
	/*
	m->m_title = "root url priority";
	m->m_desc  = "What spider priority should root urls "
		"be assigned?  Spider priorities range from 0 to 31. If no "
		"urls are scheduled to be spidered in the priority 31 "
		"bracket, the spider moves down to 30, etc., until it finds "
		"a url to spider. If this priority is undefined "
		"then that url's priority is determined based on the rules "
		"on the URL filters page. If the priority is still "
		"undefined then the priority is taken to be the priority of "
		"the parent minus one, which results in a breadth first "
		"spidering algorithm."; // html
	m->m_cgi   = "srup";
	m->m_off   = (char *)&cr.m_spiderdbRootUrlPriority - x;
	m->m_type  = TYPE_PRIORITY2;// 0-(MAX_SPIDER_PRIORITIES-1)dropdown menu
	m->m_def   = "15"; 
	m++;
	*/

	/*
	  -- mdw, now in urlfilters using "isaddurl" "reg exp"
	m->m_title = "add url priority";
	m->m_desc  = "What is the priority of a url which "
		"is added to the spider queue via the "
		"add url page?"; // html
	m->m_cgi   = "saup";
	m->m_off   = (char *)&cr.m_spiderdbAddUrlPriority - x;
	m->m_type  = TYPE_PRIORITY; // 0-(MAX_SPIDER_PRIORITIES-1)dropdown menu
	m->m_def   = "16";
	m->m_group = 0;
	m++;
	*/

	/*
	m->m_title = "new spider by priority";
	m->m_desc   = "Specify priorities for which "
		"new urls not yet in the index should be spidered.";
	m->m_cgi   = "sn";
	m->m_xml   = "spiderNewBits";
	m->m_off   = (char *)&cr.m_spiderNewBits - x;
	m->m_type  = TYPE_PRIORITY_BOXES; // array of numbered (0-(MAX_SPIDER_PRIORITIES-1)) checkboxes
	m->m_fixed = MAX_SPIDER_PRIORITIES;
	m->m_def   = "1"; // default for each one is on
	m++;

	m->m_title = "old spider by priority";
	m->m_desc  = "Specify priorities for which old "
		"urls already in the index should be spidered.";
	m->m_cgi   = "so";
	m->m_xml   = "spiderOldBits";
	m->m_off   = (char *)&cr.m_spiderOldBits - x;
	m->m_type  = TYPE_PRIORITY_BOXES; // array of numbered (0-(MAX_SPIDER_PRIORITIES-1)) checkboxes
	m->m_fixed = MAX_SPIDER_PRIORITIES;
	m->m_def   = "1"; // default for each one is on
	m->m_group = 0;
	m++;

	m->m_title = "max spiders per domain";
	m->m_desc  = "How many pages should the spider "
		"download simultaneously from any one domain? This can "
		"prevents the spider from hitting one server too hard.";
	m->m_cgi   = "mspd";
	m->m_off   = (char *)&cr.m_maxSpidersPerDomain - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "1";
	m++;

	m->m_title = "same domain wait";
	m->m_desc = "How many milliseconds should Gigablast wait "
		"between spidering a second url from the same domain. "
		"This is used to prevent the spiders from hitting a "
		"website too hard.";
	m->m_cgi = "sdw";
	m->m_off = (char *)&cr.m_sameDomainWait - x;
	m->m_type = TYPE_LONG;
	m->m_def = "500";
	m->m_group = 0;
	m++;

	m->m_title = "same ip wait";
	m->m_desc  = "How many milliseconds should Gigablast wait "
		"between spidering a second url from the same IP address. "
		"This is used to prevent the spiders from hitting a "
		"website too hard.";
	m->m_cgi   = "siw";
	m->m_off   = (char *)&cr.m_sameIpWait - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "10000";
	m->m_group = 0;
	m++;
	*/

	/*
	m->m_title = "use distributed spider lock";
	m->m_desc  = "Enable distributed spider locking to strictly enforce "
		"same domain waits at a global level.";
	m->m_cgi   = "udsl";
	m->m_off   = (char *)&cr.m_useSpiderLocks - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_group = 0;
	m++;

	m->m_title = "distribute spider download based on ip";
	m->m_desc  = "Distribute web downloads based on the ip of the host so "
		"only one spider ip hits the same hosting ip.  Helps "
		"webmaster's logs look nicer.";
	m->m_cgi   = "udsd";
	m->m_off   = (char*)&cr.m_distributeSpiderGet - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_group = 0;
	m++;

	m->m_title = "percent of water mark to reload queues";
	m->m_desc  = "When a spider queue drops below this percent of its "
		"max level it will reload from disk.";
	m->m_cgi   = "rlqp";
	m->m_off   = (char*)&cr.m_reloadQueuePercent - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "25";
	m++;
	 */

	/*
	m->m_title = "min respider wait";
	m->m_desc  = "What is the minimum number of days "
		"the spider should wait before re-visiting a particular "
		"web page? "
		"The spiders attempts to determine the update cycle of "
		"each web page and it tries to visit them as needed, but it "
		"will not wait less than this number of days regardless.";
	m->m_cgi   = "mrw";
	m->m_off   = (char *)&cr.m_minRespiderWait - x;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "1.0";
	m++;

	m->m_title = "max respider wait";
	m->m_desc  = "What is the maximum number of days "
		"the spider should wait before re-visiting a particular "
		"web page?";
	m->m_cgi   = "xrw";
	m->m_off   = (char *)&cr.m_maxRespiderWait - x;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "90.0";
	m->m_group = 0;
	m++;

	m->m_title = "first respider wait";
	m->m_desc  = "What is the number of days "
		"Gigablast should wait before spidering a particular web page "
		"for the second time? Tag in ruleset will override this value "
		"if it is present.";
	m->m_cgi   = "frw";
	m->m_off   = (char *)&cr.m_firstRespiderWait - x;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "30.0";
	m->m_group = 0;
	m++;

	m->m_title = "error respider wait";
	m->m_desc  = "If a spidered web page has a network "
		"error, such as a DNS not found error, or a time out error, "
		"how many days should Gigablast wait before reattempting "
		"to spider that web page?";
	m->m_cgi   = "erw";
	m->m_off   = (char *)&cr.m_errorRespiderWait - x;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "2.0";
	m->m_group = 0;
	m++;

	m->m_title = "doc not found error respider wait";
	m->m_desc  = "If a spidered web page has a http status "
		"error, such as a 404 page not found error, "
		"how many days should Gigablast wait before reattempting "
		"to spider that web page?";
	m->m_cgi   = "dnferw";
	m->m_off   = (char *)&cr.m_docNotFoundErrorRespiderWait - x;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "7.0";
	m->m_group = 0;
	m++;
	*/

	/*
	m->m_title = "spider max kbps";
	m->m_desc  = "The maximum kilobits per second "
		  "that the spider can download.";
	m->m_cgi   = "cmkbps";
	m->m_off   = (char *)&cr.m_maxKbps - x;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "999999.0";
	m++;

	m->m_title = "spider max pages per second";
	m->m_desc  = "The maximum number of pages per "
		"second that can be indexed or deleted from the index.";
	m->m_cgi   = "cmpps";
	m->m_off   = (char *)&cr.m_maxPagesPerSecond - x;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "999999.0";
	m->m_group = 0;
	m++;

	*/

	/*
	m->m_title = "spider new percent";
	m->m_desc  = "Approximate percentage of new vs. old docs to spider. "
		     "If set to a negative number, the old alternating "
		     "priority algorithm is used.";
	m->m_cgi   = "snp";
	m->m_off   = (char *)&cr.m_spiderNewPct - x;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "-1.0";
	m->m_group = 0;
	m++;
	*/

	/*
	m->m_title = "number retries per url";
	m->m_desc  = "How many times should the spider be "
		"allowed to fail to download a particular web page before "
		"it gives up? "
		"Failure may result from temporary loss of internet "
		"connectivity on the remote end, dns or routing problems.";
	m->m_cgi   = "nr";
	m->m_off   = (char *)&cr.m_numRetries - x;
	m->m_type  = TYPE_RETRIES; // dropdown from 0 to 3
	m->m_def   = "1";
	m++;

	m->m_title = "priority of urls being retried";
	m->m_desc  = "Keep this pretty high so that we get problem urls "
		"out of the index fast, otherwise, you might be waiting "
		"months for another retry. Use <i>undefined</i> to indicate "
		"no change in the priority of the url.";
	m->m_cgi   = "rtp";
	m->m_off   = (char *)&cr.m_retryPriority - x;
	m->m_type  = TYPE_PRIORITY2; // -1 to 31
	m->m_def   = "-1";
	m->m_group = 0;
	m++;

	m->m_title = "max pages in index";
	m->m_desc  = "What is the maximum number of "
		"pages that are permitted for this collection?";
	m->m_cgi   = "mnp";
	m->m_off   = (char *)&cr.m_maxNumPages - x;
	m->m_type  = TYPE_LONG_LONG;
	m->m_def   = "10000000000"; // 10 billion
	m++;

	m->m_title = "import link info"; //  from other cluster";
	m->m_desc  = "Say yes here to make Gigablast import "
		"link text from another collection into this one "
		"when spidering urls. Gigablast will "
		"use the hosts.conf file in the working directory to "
		"tell it what hosts belong to the cluster to import from. "
		"Gigablast "
		"will use the \"update link votes frequency\" parm above "
		"to determine if the info should be recomputed on the other "
		"cluster.";
	m->m_cgi   = "eli"; // external link info
	m->m_off   = (char *)&cr.m_getExternalLinkInfo - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_priv  = 2;
	m++;

	m->m_title = "use hosts2.conf for import cluster";
	m->m_desc  = "Tell Gigablast to import from the cluster defined by "
		"hosts2.conf in the working directory, rather than "
		"hosts.conf";
	m->m_cgi   = "elib"; // external link info
	m->m_off   = (char *)&cr.m_importFromHosts2Conf - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_priv  = 2;
	m->m_group = 0;
	m++;

	//m->m_title = "get link info from other cluster in real-time";
	//m->m_desc  = "Say yes here to make Gigablast tell the other "
	//	"cluster to compute the link info, not just return a "
	//	"stale copy from the last time it computed it.";
	//m->m_cgi   = "elif"; // external link info fresh
	//m->m_off   = (char *)&cr.m_getExternalLinkInfoFresh - x;
	//m->m_type  = TYPE_BOOL;
	//m->m_def   = "0";
	//m->m_group = 0;
	//m->m_priv  = 2;
	//m++;

	m->m_title = "collection to import from";
	m->m_desc  = "Gigablast will fetch the link info from this "
		"collection.";
	m->m_cgi   = "elic"; // external link info
	m->m_off   = (char *)&cr.m_externalColl - x;
	m->m_type  = TYPE_STRING;
	m->m_size  = MAX_COLL_LEN+1;
	m->m_def   = "";
	m->m_group = 0;
	m->m_priv  = 2;
	m++;

	m->m_title = "turk tags to display";
	m->m_desc  = "Tell pageturk to display the tag questions "
	             "for the comma seperated tag names."
		     " no space allowed.";
        m->m_cgi   = "ttags";
	m->m_xml   = "turkTags";
	m->m_type  = TYPE_STRING;
	m->m_size  = 256;
	m->m_def   = "blog,spam,news";
	m->m_off   = (char *)&cr.m_turkTags - x;
	m->m_group = 0;
	m->m_priv  = 2;
	m++;
	*/

	/*
	// now we store this in title recs, so we can change it on the fly
	m->m_title = "title weight";
	m->m_desc  = "Weight title this much more or less. This units are "
		"percentage. A 100 means to not give the title any special "
		"weight. Generally, though, you want to give it significantly "
		"more weight than that, so 2400 is the default.";
	m->m_cgi   = "tw"; 

	m->m_off   = (char *)&cr.m_titleWeight - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "4600";
	m->m_min   = 0;
	m++;

	// now we store this in title recs, so we can change it on the fly
	m->m_title = "header weight";
	m->m_desc  = "Weight terms in header tags by this much more or less. "
		"This units are "
		"percentage. A 100 means to not give the header any special "
		"weight. Generally, though, you want to give it significantly "
		"more weight than that, so 600 is the default.";
	m->m_cgi   = "hw"; 
	m->m_off   = (char *)&cr.m_headerWeight - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "600";
	m->m_min   = 0;
	m->m_group = 0;
	m++;

	// now we store this in title recs, so we can change it on the fly
	m->m_title = "url path word weight";
	m->m_desc  = "Weight text in url path this much more. "
		"The units are "
		"percentage. A 100 means to not give any special "
		"weight. Generally, though, you want to give it significantly "
		"more weight than that, so 600 is the default.";
	m->m_cgi   = "upw"; 
	m->m_off   = (char *)&cr.m_urlPathWeight - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "1600";
	m->m_min   = 0;
	m->m_group = 0;
	m++;

	// now we store this in title recs, so we can change it on the fly
	m->m_title = "external link text weight";
	m->m_desc  = "Weight text in the incoming external link text this "
		"much more. The units are percentage. It already receives a "
		"decent amount of weight naturally.";
	m->m_cgi   = "eltw"; 
	m->m_off   = (char *)&cr.m_externalLinkTextWeight - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "600";
	m->m_min   = 0;
	m->m_group = 0;
	m++;

	// now we store this in title recs, so we can change it on the fly
	m->m_title = "internal link text weight";
	m->m_desc  = "Weight text in the incoming internal link text this "
		"much more. The units are percentage. It already receives a "
		"decent amount of weight naturally.";
	m->m_cgi   = "iltw"; 
	m->m_off   = (char *)&cr.m_internalLinkTextWeight - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "200";
	m->m_min   = 0;
	m->m_group = 0;
	m++;

	// now we store this in title recs, so we can change it on the fly
	m->m_title = "concept weight";
	m->m_desc  = "Weight concepts this much more. "
		"The units are "
		"percentage. It already receives a decent amount of weight "
		"naturally. AKA: surrounding text boost.";
	m->m_cgi   = "cw"; 
	m->m_off   = (char *)&cr.m_conceptWeight - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "50";
	m->m_min   = 0;
	m->m_group = 0;
	m++;
	*/

	/*
	// now we store this in title recs, so we can change it on the fly
	m->m_title = "site num inlinks boost base";
	m->m_desc  = "Boost the score of all terms in the document using "
		"this number. "
		"The boost itself is expressed as a percentage. "
		"The boost is B^X, where X is the number of good "
		"inlinks to the document's site "
		"and B is this is this boost base. "
		"The score of each term in the "
		"document is multiplied by the boost. That product "
		"becomes the new score of that term. "
		"For purposes of this calculation we limit X to 1000.";
	m->m_cgi   = "qbe"; 
	m->m_off   = (char *)&cr.m_siteNumInlinksBoostBase - x;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "1.005";
	m->m_min   = 0;
	m->m_group = 0;
	m++;
	*/

	/*
	// use menu elimination technology?
	m->m_title = "only index article content";
	m->m_desc  = "If this is true gigablast will only index the "
		"article content on pages identifed as permalinks. It will "
		"NOT index any page content on non-permalink pages, and it "
		"will avoid indexing menu content on any page. It will not "
		"index meta tags on any page. It will only index incoming "
		"link text for permalink pages. Useful when "
		"indexing blog or news sites.";
	m->m_cgi   = "met";
	m->m_off   = (char *)&cr.m_eliminateMenus - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m++;
	*/

	// replace by lang== lang!= in url filters
	//m->m_title = "collection language";
	//m->m_desc  = "Only spider pages determined to be in "
	//	"this language (see Language.h)";
	//m->m_cgi   = "clang";
	//m->m_off   = (char *)&cr.m_language - x;
	//m->m_type  = TYPE_LONG;
	//m->m_def   = "0";
	//m++;

	///////////////////////////////////////////
	// SEARCH CONTROLS
	///////////////////////////////////////////


	//m->m_title = "allow RAID style list intersection";
	//m->m_desc  = "Allow using RAID style lookup for intersecting term "
	//	     "lists and getting docIds for queries.";
	//m->m_cgi   = "uraid";
	//m->m_off   = (char *)&cr.m_allowRaidLookup - x;
	//m->m_type  = TYPE_BOOL;
	//m->m_def   = "0";
	//m++;

	//m->m_title = "allow RAIDed term list read";
	//m->m_desc  = "Allow splitting up the term list read for large lists "
	//	     "amongst twins.";
	//m->m_cgi   = "ulraid";
	//m->m_off   = (char *)&cr.m_allowRaidListRead - x;
	//m->m_type  = TYPE_BOOL;
	//m->m_def   = "0";
	//m->m_group = 0;
	//m++;

	//m->m_title = "max RAID mercenaries";
	//m->m_desc  = "Max number of mercenaries to use in RAID lookup and "
	//	     "intersection.";
	//m->m_cgi   = "raidm";
	//m->m_off   = (char *)&cr.m_maxRaidMercenaries - x;
	//m->m_type  = TYPE_LONG;
	//m->m_def   = "2";
	//m->m_group = 0;
	//m++;

	//m->m_title = "min term list size to RAID";
	//m->m_desc  = "Term list size to begin doing term list RAID";
	//m->m_cgi   = "raidsz";
	//m->m_off   = (char *)&cr.m_minRaidListSize - x;
	//m->m_type  = TYPE_LONG;
	//m->m_def   = "1000000";
	//m->m_group = 0;
	//m++;

	m->m_title = "restrict indexdb for queries";
	m->m_desc  = "If this is true Gigablast will only search the root "
		"index file for docIds. Saves on disk seeks, "
		"but may use older versions of indexed web pages.";
	m->m_cgi   = "riq";
	m->m_off   = (char *)&cr.m_restrictIndexdbForQuery - x;
	m->m_type  = TYPE_BOOL;
	m->m_page  = PAGE_SEARCH;
	m->m_def   = "0";
	m->m_sparm = 1;
	m->m_scgi  = "ri";
	m->m_soff  = (char *)&si.m_restrictIndexdbForQuery - y;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "restrict indexdb for xml feed";
	m->m_desc  = "Like above, but specifically for XML feeds.";
	m->m_cgi   = "rix";
	m->m_off   = (char *)&cr.m_restrictIndexdbForXML - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	//m->m_title = "restrict indexdb for queries in xml feed";
	//m->m_desc  = "Same as above, but just for the XML feed.";
	//m->m_cgi   = "riqx";
	//m->m_off   = (char *)&cr.m_restrictIndexdbForQueryRaw - x;
	//m->m_type  = TYPE_BOOL;
	//m->m_def   = "1";
	//m->m_group = 0;
	//m++;

	m->m_title = "read from cache by default";
	m->m_desc  = "Should we read search results from the cache? Set "
		"to false to fix dmoz bug.";
	m->m_cgi   = "rcd";
	m->m_off   = (char *)&cr.m_rcache - x;
	m->m_soff  = (char *)&si.m_rcache - y;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_sparm = 1;
	m->m_scgi  = "rcache";
	m->m_sprpg = 0;
	m->m_sprpp = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "do spell checking";
	m->m_desc  = "If enabled while using the XML feed, "
		"when Gigablast finds a spelling recommendation it will be "
		"included in the XML <spell> tag. Default is 0 if using an "
		"XML feed, 1 otherwise.";
	m->m_cgi   = "spell";
	m->m_off   = (char *)&cr.m_spellCheck - x;
	m->m_soff  = (char *)&si.m_spellCheck - y;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_sparm = 1;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "get docid scoring info";
	m->m_desc  = "Get scoring information for each result so you "
		"can see how each result is scored? You must explicitly "
		"request this using &scores=1 for the XML feed because it "
		"is not included by default.";
	m->m_cgi   = "scores"; // dedupResultsByDefault";
	m->m_off   = (char *)&cr.m_getDocIdScoringInfo - x;
	m->m_soff  = (char *)&si.m_getDocIdScoringInfo - y;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_sparm = 1;
	m->m_scgi  = "scores";
	m++;

	m->m_title = "do query expansion";
	m->m_desc  = "Query expansion will include word stems and synonyms in "
		"its search results.";
	m->m_def   = "1";
	m->m_off   = (char *)&cr.m_queryExpansion - x;
	m->m_soff  = (char *)&si.m_queryExpansion - y;
	m->m_type  = TYPE_BOOL;
	m->m_sparm = 1;
	m->m_cgi  = "qe";
	m->m_scgi  = "qe";
	m++;


	// more general parameters
	m->m_title = "max search results";
	m->m_desc  = "What is the limit to the total number "
		"of returned search results.";
	m->m_cgi   = "msr";
	m->m_off   = (char *)&cr.m_maxSearchResults - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "1000";
	m++;

	m->m_title = "max search results per query";
	m->m_desc  = "What is the limit to the total number "
		"of returned search results per query?";
	m->m_cgi   = "msrpq";
	m->m_off   = (char *)&cr.m_maxSearchResultsPerQuery - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "100";
	m++;

	m->m_title = "max search results for paying clients";
	m->m_desc  = "What is the limit to the total number "
		"of returned search results for clients.";
	m->m_cgi   = "msrfpc";
	m->m_off   = (char *)&cr.m_maxSearchResultsForClients - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "1000";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "max search results per query for paying clients";
	m->m_desc  = "What is the limit to the total number "
		"of returned search results per query for paying clients? "
		"Auto ban must be enabled for this to work.";
	m->m_cgi   = "msrpqfc";
	m->m_off   = (char *)&cr.m_maxSearchResultsPerQueryForClients - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "1000";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;



	m->m_title = "max title len";
	m->m_desc  = "What is the maximum number of "
		"characters allowed in titles displayed in the search "
		"results?";
	m->m_cgi   = "tml";
	m->m_off   = (char *)&cr.m_titleMaxLen - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "80";
	m++;

	m->m_title = "consider titles from body";
	m->m_desc = "Can Gigablast make titles from the document content? "
		"Used mostly for the news collection where the title tags "
		"are not very reliable.";
	m->m_cgi   = "gtfb";
	m->m_off   = (char *)&cr.m_considerTitlesFromBody - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_sparm = 1;
	m->m_soff  = (char *)&si.m_considerTitlesFromBody - y;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;


	m->m_title = "site cluster by default";
	m->m_desc  = "Should search results be site clustered by default?";
	m->m_cgi   = "scd";
	m->m_off   = (char *)&cr.m_siteClusterByDefault - x;
	m->m_soff  = (char *)&si.m_doSiteClustering - y;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_sparm = 1;
	m->m_scgi  = "sc";
	m++;

	m->m_title = "use min ranking algo";
	m->m_desc  = "Should search results be ranked using this algo?";
	//m->m_cgi   = "uma";
	//m->m_off   = (char *)&cr.m_siteClusterByDefault - x;
	m->m_soff  = (char *)&si.m_useMinAlgo - y;
	m->m_type  = TYPE_BOOL;
	m->m_obj   = OBJ_SI;
	// seems, good, default it on
	m->m_def   = "1";
	m->m_sparm = 1;
	m->m_scgi  = "uma";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;


	// limit to this # of the top term pairs from inlink text whose
	// score is accumulated
	m->m_title = "real max top";
	m->m_desc  = "Only score up to this many inlink text term pairs";
	m->m_soff  = (char *)&si.m_realMaxTop - y;
	m->m_type  = TYPE_LONG;
	m->m_obj   = OBJ_SI;
	m->m_def   = "10";
	m->m_sparm = 1;
	m->m_scgi  = "mit";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "use new ranking algo";
	m->m_desc  = "Should search results be ranked using this new algo?";
	m->m_soff  = (char *)&si.m_useNewAlgo - y;
	m->m_type  = TYPE_BOOL;
	m->m_obj   = OBJ_SI;
	// seems, good, default it on
	m->m_def   = "1";
	m->m_sparm = 1;
	m->m_scgi  = "una";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "do max score algo";
	m->m_desc  = "Quickly eliminated docids using max score algo";
	m->m_soff  = (char *)&si.m_doMaxScoreAlgo - y;
	m->m_type  = TYPE_BOOL;
	m->m_obj   = OBJ_SI;
	m->m_def   = "1";
	m->m_sparm = 1;
	m->m_scgi  = "dmsa";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;


	m->m_title = "use fast intersection algo";
	m->m_desc  = "Should we try to speed up search results generation?";
	m->m_soff  = (char *)&si.m_fastIntersection - y;
	m->m_type  = TYPE_CHAR;
	m->m_obj   = OBJ_SI;
	// turn off until we debug
	m->m_def   = "-1";
	m->m_sparm = 1;
	m->m_scgi  = "fi";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;


	// buzz
	m->m_title = "hide all clustered results";
	m->m_desc  = "Hide all clustered results instead of displaying two "
		"results from each site.";
	m->m_cgi   = "hacr";
	m->m_off   = (char *)&cr.m_hideAllClustered - x;
	m->m_type  = TYPE_BOOL;
	m->m_obj   = OBJ_COLL;
	m->m_def   = "0";
	m->m_group = 0;
	m++;

	m->m_title = "dedup results by default";
	m->m_desc  = "Should duplicate search results be removed by default?";
	m->m_cgi   = "drd"; // dedupResultsByDefault";
	m->m_off   = (char *)&cr.m_dedupResultsByDefault - x;
	m->m_soff  = (char *)&si.m_doDupContentRemoval - y;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_group = 1;
	m->m_sparm = 1;
	m->m_scgi  = "dr";
	m++;

	m->m_title = "dedup URLs";
	m->m_desc  = "Should we dedup URLs with case insensitivity? This is "
                     "mainly to correct duplicate wiki pages.";
	m->m_cgi   = "ddu";
	m->m_off   = (char *)&cr.m_dedupURLDefault - x;
	m->m_soff  = (char *)&si.m_dedupURL - y;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_sparm = 1;
	m->m_scgi  = "ddu";
	m++;

	m->m_title = "percent similar dedup summary";
	m->m_desc  = "If document summary is this percent similar "
		"to a document summary above it, then remove it from the search "
		"results. 100 means only to remove if exactly the same. 0 means"
		" no summary deduping.";
	m->m_cgi   = "psds";
	m->m_off   = (char *)&cr.m_percentSimilarSummary - x;
	m->m_soff  = (char *)&si.m_percentSimilarSummary - y;
	m->m_type  = TYPE_LONG;
	m->m_def   = "90";
	m->m_group = 0;
	m->m_sparm = 1;
	m->m_scgi  = "pss";
	m->m_smin  = 0;
	m->m_smax  = 100;
	m++;       

	m->m_title = "number of lines to use in summary to dedup";
	m->m_desc  = "Sets the number of lines to generate for summary deduping."
		" This is to help the deduping process not thorw out valid "
		"summaries when normally displayed summaries are smaller values."
		" Requires percent similar dedup summary to be enabled.";
	m->m_cgi   = "msld";
	m->m_off   = (char *)&cr.m_summDedupNumLines - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "4";
	m->m_group = 0;
	m++;       
	


	m->m_title = "use vhost language detection";
	m->m_desc  = "Use language specific pages for home, etc.";
	m->m_cgi   = "vhost";
	m->m_off   = (char *)&cr.m_useLanguagePages - x;
	m->m_soff  = (char *)&si.m_useLanguagePages - y;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_sparm = 1;
	m->m_scgi  = "vhost";
	m->m_smin  = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	/*
	m->m_title = "special query";
	m->m_desc  = "List of docids to restrain results to.";
	m->m_cgi   = "sq";
	m->m_soff  = (char *)&si.m_sq - y;
	m->m_type  = TYPE_STRING;
	m->m_size  = 6; // up to 5 chars + NULL, e.g. "en_US"
	m->m_def   = "en_US";
	m->m_group = 0;
	m->m_sparm = 1;
	m->m_scgi  = "sq";
	m++;
	*/

	m->m_title = "use language weights";
	m->m_desc  = "Use Language weights to sort query results. "
		"This will give results that match the specified &qlang "
		"higher ranking.";
	m->m_cgi   = "lsort";
	m->m_off   = (char *)&cr.m_enableLanguageSorting - x;
	m->m_soff  = (char *)&si.m_enableLanguageSorting - y;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_group = 1;
	m->m_sparm = 1;
	m->m_scgi  = "lsort";
	m->m_smin  = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "sort language preference";
	m->m_desc  = "Default language to use for ranking results. "
		//"This should only be used on limited collections. "
		"Value should be any language abbreviation, for example "
		"\"en\" for English.";
	m->m_cgi   = "qlang";
	m->m_off   = (char *)&cr.m_defaultSortLanguage - x;
	m->m_soff  = (char *)&si.m_defaultSortLanguage - y;
	m->m_type  = TYPE_STRING;
	m->m_size  = 6; // up to 5 chars + NULL, e.g. "en_US"
	m->m_def   = "en";//_US";
	m->m_group = 0;
	m->m_sparm = 1;
	m->m_scgi  = "qlang";
	m++;

	m->m_title = "sort country preference";
	m->m_desc  = "Default country to use for ranking results. "
		//"This should only be used on limited collections. "
		"Value should be any country code abbreviation, for example "
		"\"us\" for United States.";
	m->m_cgi   = "qcountry";
	m->m_off   = (char *)&cr.m_defaultSortCountry - x;
	m->m_soff  = (char *)&si.m_defaultSortCountry - y;
	m->m_type  = TYPE_STRING;
	m->m_size  = 2+1;
	m->m_def   = "us";
	m->m_group = 0;
	m->m_sparm = 1;
	m->m_scgi  = "qcountry";
	m++;

	/*
	m->m_title = "language method weights";
	m->m_desc  = "Language method weights for spider language "
		"detection. A string of ascii numerals that "
		"should default to 895768712";
	m->m_cgi   = "lmweights";
	m->m_off   = (char *)&cr.m_languageMethodWeights - x;
	m->m_type  = TYPE_STRING;
	m->m_size  = 10; // up to 9 chars + NULL
	m->m_def   = "894767812";
	m->m_group = 0;
	// m->m_sparm = 1;
	m++;

	m->m_title = "language detection sensitivity";
	m->m_desc  = "Language detection sensitivity. Higher"
		" values mean higher hitrate, but lower accuracy."
		" Suggested values are from 2 to 20";
	m->m_cgi   = "lmbailout";
	m->m_off   = (char *)&cr.m_languageBailout - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "5";
	m->m_group = 0;
	// m->m_sparm = 1;
	m++;

	m->m_title = "language detection threshold";
	m->m_desc  = "Language detection threshold sensitivity."
		" Higher values mean better accuracy, but lower hitrate."
		" Suggested values are from 2 to 20";
	m->m_cgi   = "lmthreshold";
	m->m_off   = (char *)&cr.m_languageThreshold - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "3";
	m->m_group = 0;
	// m->m_sparm = 1;
	m++;

	m->m_title = "language detection samplesize";
	m->m_desc  = "Language detection size. Higher values"
		" mean more accuracy, but longer processing time."
		" Suggested values are 300-1000";
	m->m_cgi   = "lmsamples";
	m->m_off   = (char *)&cr.m_languageSamples - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "600";
	m->m_group = 0;
	// m->m_sparm = 1;
	m++;

 	m->m_title = "language detection spider samplesize";
 	m->m_desc  = "Language detection page sample size. "
 		"Higher values mean more accuracy, but longer "
 		"spider time."
 		" Suggested values are 3000-10000";
 	m->m_cgi   = "lpsamples";
 	m->m_off   = (char *)&cr.m_langPageLimit - x;
 	m->m_type  = TYPE_LONG;
 	m->m_def   = "6000";
 	m->m_group = 0;
 	// m->m_sparm = 1;
 	m++;
	*/

	// for post query reranking 
	m->m_title = "docs to check for post query demotion";
	m->m_desc  = "How many search results should we "
		"scan for post query demotion? "
		"0 disables all post query reranking. ";
	m->m_cgi   = "pqrds";
	m->m_off   = (char *)&cr.m_pqr_docsToScan - x;
	m->m_soff  = (char *)&si.m_docsToScanForReranking - y;
	m->m_type  = TYPE_LONG;
	m->m_def   = "0";
	m->m_group = 1;
	m->m_sparm = 1;
	m->m_scgi  = "pqrds";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "demotion for foreign languages";
	m->m_desc  = "Demotion factor of non-relevant languages.  Score "
		"will be penalized by this factor as a percent if "
		"it's language is foreign. "
		"A safe value is probably anywhere from 0.5 to 1. ";
	m->m_cgi   = "pqrlang";
	m->m_off   = (char *)&cr.m_languageWeightFactor - x;
	m->m_soff  = (char *)&si.m_languageWeightFactor - y;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "0.999";
	m->m_group = 0;
	m->m_sparm = 1;
	m->m_scgi  = "pqrlang";
	m->m_smin  = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "demotion for unknown languages";
	m->m_desc  = "Demotion factor for unknown languages. "
		"Page's score will be penalized by this factor as a percent "
		"if it's language is not known. "
		"A safe value is 0, as these pages will be reranked by "
		"country (see below). "
		"0 means no demotion.";
	m->m_cgi   = "pqrlangunk";
	m->m_off   = (char *)&cr.m_languageUnknownWeight- x;
	m->m_soff  = (char *)&si.m_languageUnknownWeight- y;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "0.0";
	m->m_group = 0;
	m->m_sparm = 1;
	m->m_scgi  = "pqrlangunk";
	m->m_smin  = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "demotion for pages where the country of the page writes "
		"in the same language as the country of the query";
	m->m_desc  = "Demotion for pages where the country of the page writes "
		"in the same language as the country of the query. "
		"If query language is the same as the language of the page, "
		"then if a language written in the country of the page matches "
		"a language written by the country of the query, then page's "
		"score will be demoted by this factor as a percent. "
		"A safe range is between 0.5 and 1. ";
	m->m_cgi   = "pqrcntry";
	m->m_off   = (char *)&cr.m_pqr_demFactCountry - x;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "0.98";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "demotion for query terms or gigabits in url";
	m->m_desc  = "Demotion factor for query terms or gigabits "
		"in a result's url. "
		"Score will be penalized by this factor times the number "
		"of query terms or gigabits in the url divided by "
		"the max value below such that fewer "
		"query terms or gigabits in the url causes the result "
		"to be demoted more heavily, depending on the factor. "
		"Higher factors demote more per query term or gigabit "
		"in the page's url. "
		"Generally, a page may not be demoted more than this "
		"factor as a percent. Also, how it is demoted is "
		"dependant on the max value. For example, "
		"a factor of 0.2 will demote the page 20% if it has no "
		"query terms or gigabits in its url. And if the max value is "
		"10, then a page with 5 query terms or gigabits in its "
		"url will be demoted 10%; and 10 or more query terms or "
		"gigabits in the url will not be demoted at all. "
		"0 means no demotion. "
		"A safe range is from 0 to 0.35. ";
	m->m_cgi   = "pqrqttiu";
	m->m_off   = (char *)&cr.m_pqr_demFactQTTopicsInUrl - x;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "max value for pages with query terms or gigabits "
		"in url";
	m->m_desc  = "Max number of query terms or gigabits in a url. "
		"Pages with a number of query terms or gigabits in their "
		"urls greater than or equal to this value will not be "
		"demoted. "
		"This controls the range of values expected to represent "
		"the number of query terms or gigabits in a url. It should "
		"be set to or near the estimated max number of query terms "
		"or topics that can be in a url. Setting to a lower value "
		"increases the penalty per query term or gigabit that is "
		"not in a url, but decreases the range of values that "
		"will be demoted.";
	m->m_cgi   = "pqrqttium";
	m->m_off   = (char *)&cr.m_pqr_maxValQTTopicsInUrl - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "10";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "demotion for pages that are not high quality";
	m->m_desc  = "Demotion factor for pages that are not high quality. "
		"Score is penalized by this number as a percent times level "
		"of quality. A pqge will be demoted by the formula "
		"(max quality - page's quality) * this factor / the max "
		"value given below. Generally, a page will not be "
		"demoted more than this factor as a percent. "
		"0 means no demotion. "
		"A safe range is between 0 to 1. ";
	m->m_cgi   = "pqrqual";
	m->m_off   = (char *)&cr.m_pqr_demFactQual - x;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "max value for pages that are not high quality";
	m->m_desc  = "Max page quality. Pages with a quality level "
		"equal to or higher than this value "
		"will not be demoted. ";
	m->m_cgi   = "pqrqualm";
	m->m_off   = (char *)&cr.m_pqr_maxValQual - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "100";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "demotion for pages that are not "
		"root or have many paths in the url";
	m->m_desc  = "Demotion factor each path in the url. "
		"Score will be demoted by this factor as a percent "
		"multiplied by the number of paths in the url divided "
		"by the max value below. "
		"Generally, the page will not be demoted more than this "
		"value as a percent. "
		"0 means no demotion. "
		"A safe range is from 0 to 0.75. ";
	m->m_cgi   = "pqrpaths";
	m->m_off   = (char *)&cr.m_pqr_demFactPaths - x;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "max value for pages that have many paths in the url";
	m->m_desc  = "Max number of paths in a url. "
		"This should be set to a value representing a very high "
		"number of paths for a url. Lower values increase the "
		"difference between how much each additional path demotes. ";
	m->m_cgi   = "pqrpathsm";
	m->m_off   = (char *)&cr.m_pqr_maxValPaths - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "16";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "demotion for pages that do not have a catid";
	m->m_desc  = "Demotion factor for pages that do not have a catid. "
		"Score will be penalized by this factor as a percent. "
		"A safe range is from 0 to 0.2. ";
	m->m_cgi   = "pqrcatid";
	m->m_off   = (char *)&cr.m_pqr_demFactNoCatId - x;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "demotion for pages where smallest "
		"catid has a lot of super topics";
	m->m_desc  = "Demotion factor for pages where smallest "
		"catid has a lot of super topics. "
		"Page will be penalized by the number of super topics "
		"multiplied by this factor divided by the max value given "
		"below. "
		"Generally, the page will not be demoted more than this "
		"factor as a percent. "
		"Note: pages with no catid are demoted by this factor as "
		"a percent so as not to penalize pages with a catid. "
		"0 means no demotion. "
		"A safe range is between 0 and 0.25. ";
	m->m_cgi   = "pqrsuper";
	m->m_off   = (char *)&cr.m_pqr_demFactCatidHasSupers - x;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "max value for pages where smallest catid has a lot "
		"of super topics";
	m->m_desc  = "Max number of super topics. "
		"Pages whose smallest catid that has more super "
		"topics than this will be demoted by the maximum amount "
		"given by the factor above as a percent. "
		"This should be set to a value representing a very high "
		"number of super topics for a category id. "
		"Lower values increase the difference between how much each "
		"additional path demotes. ";
	m->m_cgi   = "pqrsuperm";
	m->m_off   = (char *)&cr.m_pqr_maxValCatidHasSupers - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "11";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "demotion for larger pages";
	m->m_desc  = "Demotion factor for larger pages. "
		"Page will be penalized by its size times this factor "
		"divided by the max page size below. "
		"Generally, a page will not be demoted more than this "
		"factor as a percent. "
		"0 means no demotion. "
		"A safe range is between 0 and 0.25. ";
	m->m_cgi   = "pqrpgsz";
	m->m_off   = (char *)&cr.m_pqr_demFactPageSize - x;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "max value for larger pages";
	m->m_desc  = "Max page size. "
		"Pages with a size greater than or equal to this will be "
		"demoted by the max amount (the factor above as a percent). ";
	m->m_cgi   = "pqrpgszm";
	m->m_off   = (char *)&cr.m_pqr_maxValPageSize - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "524288";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "demotion for non-location specific queries "
		"with a location specific title";
	m->m_desc  = "Demotion factor for non-location specific queries "
		"with a location specific title. "
		"Pages which contain a location in their title which is "
		"not in the query or the gigabits will be demoted by their "
		"population multiplied by this factor divided by the max "
		"place population specified below. "
		"Generally, a page will not be demoted more than this "
		"value as a percent. "
		"0 means no demotion. ";
	m->m_cgi   = "pqrloct";
	m->m_off   = (char *)&cr.m_pqr_demFactLocTitle - x;
	m->m_sparm = 1;
	m->m_scgi  = "pqrloct";
	m->m_soff  = (char *)&si.m_pqr_demFactLocTitle - y;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "0.99";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "demotion for non-location specific queries "
		"with a location specific summary";
	m->m_desc  = "Demotion factor for non-location specific queries "
		"with a location specific summary. "
		"Pages which contain a location in their summary which is "
		"not in the query or the gigabits will be demoted by their "
		"population multiplied by this factor divided by the max "
		"place population specified below. "
		"Generally, a page will not be demoted more than this "
		"value as a percent. "
		"0 means no demotion. ";
	m->m_cgi   = "pqrlocs";
	m->m_off   = (char *)&cr.m_pqr_demFactLocSummary - x;
	m->m_sparm = 1;
	m->m_scgi  = "pqrlocs";
	m->m_soff  = (char *)&si.m_pqr_demFactLocSummary - y;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "0.95";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "demotion for non-location specific queries "
		"with a location specific dmoz category";
	m->m_desc  = "Demotion factor for non-location specific queries "
		"with a location specific dmoz regional category. "
		"Pages which contain a location in their dmoz which is "
		"not in the query or the gigabits will be demoted by their "
		"population multiplied by this factor divided by the max "
		"place population specified below. "
		"Generally, a page will not be demoted more than this "
		"value as a percent. "
		"0 means no demotion. ";
	m->m_cgi   = "pqrlocd";
	m->m_off   = (char *)&cr.m_pqr_demFactLocDmoz - x;
	m->m_sparm = 1;
	m->m_scgi  = "pqrlocd";
	m->m_soff  = (char *)&si.m_pqr_demFactLocDmoz - y;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "0.95";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "demote locations that appear in gigabits";
	m->m_desc  = "Demote locations that appear in gigabits.";
	m->m_cgi   = "pqrlocg";
	m->m_off   = (char *)&cr.m_pqr_demInTopics - x;
	m->m_sparm = 1;
	m->m_scgi  = "pqrlocg";
	m->m_soff  = (char *)&si.m_pqr_demInTopics - y;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "max value for non-location specific queries "
		"with location specific results";
	m->m_desc  = "Max place population. "
		"Places with a population greater than or equal to this "
		"will be demoted to the maximum amount given by the "
		"factor above as a percent. ";
	m->m_cgi   = "pqrlocm";
	m->m_off   = (char *)&cr.m_pqr_maxValLoc - x;
	m->m_type  = TYPE_LONG;
	// charlottesville was getting missed when this was 1M
	m->m_def   = "100000";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "demotion for non-html";
	m->m_desc  = "Demotion factor for content type that is non-html. "
		"Pages which do not have an html content type will be "
		"demoted by this factor as a percent. "
		"0 means no demotion. "
		"A safe range is between 0 and 0.35. ";
	m->m_cgi   = "pqrhtml";
	m->m_off   = (char *)&cr.m_pqr_demFactNonHtml - x;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "demotion for xml";
	m->m_desc  = "Demotion factor for content type that is xml. "
		"Pages which have an xml content type will be "
		"demoted by this factor as a percent. "
		"0 means no demotion. "
		"Any value between 0 and 1 is safe if demotion for non-html "
		"is set to 0. Otherwise, 0 should probably be used. ";
	m->m_cgi   = "pqrxml";
	m->m_off   = (char *)&cr.m_pqr_demFactXml - x;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "0.95";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "demotion for pages with other pages from same "
		"hostname";
	m->m_desc  = "Demotion factor for pages with fewer other pages from "
		"same hostname. "
		"Pages with results from the same host will be "
		"demoted by this factor times each fewer host than the max "
		"value given below, divided by the max value. "
		"Generally, a page will not be demoted more than this "
		"factor as a percent. "
		"0 means no demotion. "
		"A safe range is between 0 and 0.35. ";
	m->m_cgi   = "pqrfsd";
	m->m_off   = (char *)&cr.m_pqr_demFactOthFromHost - x;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "max value for pages with other pages from same "
		"domain";
	m->m_desc  = "Max number of pages from same domain. "
		"Pages which have this many or more pages from the same "
		"domain will not be demoted. "; 
	m->m_cgi   = "pqrfsdm";
	m->m_off   = (char *)&cr.m_pqr_maxValOthFromHost - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "12";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "initial demotion for pages with common "
		"topics in dmoz as other results";
	m->m_desc  = "Initial demotion factor for pages with common "
		"topics in dmoz as other results. "
		"Pages will be penalized by the number of common topics "
		"in dmoz times this factor divided by the max value "
		"given below. "
		"Generally, a page will not be demoted by more than this "
		"factor as a percent. "
		"Note: this factor is decayed by the factor specified in "
		"the parm below, decay for pages with common topics in "
		"dmoz as other results, as the number of pages with "
		"common topics in dmoz increases. "
		"0 means no demotion. "
		"A safe range is between 0 and 0.35. ";
	m->m_cgi   = "pqrctid";
	m->m_off   = (char *)&cr.m_pqr_demFactComTopicInDmoz - x;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "decay for pages with common topics in dmoz "
		"as other results";
	m->m_desc  = "Decay factor for pages with common topics in "
		"dmoz as other results. "
		"The initial demotion factor will be decayed by this factor "
		"as a percent as the number of common topics increase. "
		"0 means no decay. "
		"A safe range is between 0 and 0.25. ";
	m->m_cgi   = "pqrctidd";
	m->m_off   = (char *)&cr.m_pqr_decFactComTopicInDmoz - x;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "max value for pages with common topics in dmoz "
		"as other results";
	m->m_desc  = "Max number of common topics in dmoz as other results. "
		"Pages with a number of common topics equal to or greater "
		"than this value will be demoted to the maximum as given "
		"by the initial factor above as a percent. ";
	m->m_cgi   = "pqrctidm";
	m->m_off   = (char *)&cr.m_pqr_maxValComTopicInDmoz - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "32"; 
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "demotion for pages where dmoz category names "
		"contain query terms or their synonyms";
	m->m_desc  = "Demotion factor for pages where dmoz category names "
		"contain fewer query terms or their synonyms. "
		"Pages will be penalized for each query term or synonym of "
		"a query term less than the max value given below multiplied "
		"by this factor, divided by the max value. "
		"Generally, a page will not be demoted more than this value "
		"as a percent. "
		"0 means no demotion. "
		"A safe range is between 0 and 0.3. ";
	m->m_cgi   = "pqrdcndcqt";
	m->m_off   = (char *)&cr.m_pqr_demFactDmozCatNmNoQT - x;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;
	
	m->m_title = "max value for pages where dmoz category names "
		"contain query terms or their synonyms";
	m->m_desc  = "Max number of query terms and their synonyms "
		"in a page's dmoz category name. "
		"Pages with a number of query terms or their synonyms in all "
		"dmoz category names greater than or equal to this value "
		"will not be demoted. ";
	m->m_cgi   = "pqrcndcqtm";
	m->m_off   = (char *)&cr.m_pqr_maxValDmozCatNmNoQT - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "10"; 
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "demotion for pages where dmoz category names "
		"contain gigabits";
	m->m_desc  = "Demotion factor for pages where dmoz category "
		"names contain fewer gigabits. "
		"Pages will be penalized by the number of gigabits in all "
		"dmoz category names fewer than the max value given below "
		"divided by the max value. "
		"Generally, a page will not be demoted more than than this "
		"factor as a percent. "
		"0 means no demotion. "
		"A safe range is between 0 and 0.3. ";
	m->m_cgi   = "pqrdcndcgb";
	m->m_off   = (char *)&cr.m_pqr_demFactDmozCatNmNoGigabits - x;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "max value for pages where dmoz category names "
		"contain gigabits";
	m->m_desc  = "Max number of pages where dmoz category names "
		"contain a gigabit. "
		"Pages with a number of gigabits in all dmoz category names "
		"greater than or equal to this value will not be demoted. ";
	m->m_cgi   = "pqrdcndcgbm";
	m->m_off   = (char *)&cr.m_pqr_maxValDmozCatNmNoGigabits - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "16"; 
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "demotion for pages based on datedb date";
	m->m_desc  = "Demotion factor for pages based on datedb date. "
		"Pages will be penalized for being published earlier than the "
		"max date given below. "
		"The older the page, the more it will be penalized based on "
		"the time difference between the page's date and the max date, "
		"divided by the max date. "
		"Generally, a page will not be demoted more than this "
		"value as a percent. "
		"0 means no demotion. "
		"A safe range is between 0 and 0.4. ";
	m->m_cgi   = "pqrdate";
	m->m_off   = (char *)&cr.m_pqr_demFactDatedbDate - x;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;
	
	m->m_title = "min value for demotion based on datedb date ";
	m->m_desc  = "Pages with a publish date equal to or earlier than "
		"this date will be demoted to the max (the factor above as "
		"a percent). "
		"Use this parm in conjunction with the max value below "
		"to specify the range of dates where demotion occurs. "
		"If you set this parm near the estimated earliest publish "
		"date that occurs somewhat frequently, this method can better "
		"control the additional demotion per publish day. "
		"This number is given as seconds since the epoch, January 1st, "
		"1970 divided by 1000. "
		"0 means use the epoch. ";
	m->m_cgi   = "pqrdatei";
	m->m_off   = (char *)&cr.m_pqr_minValDatedbDate - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "631177"; // Jan 01, 1990 
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "max value for demotion based on datedb date ";
	m->m_desc  = "Pages with a publish date greater than or equal to "
		"this value divided by 1000 will not be demoted. "
		"Use this parm in conjunction with the min value above "
		"to specify the range of dates where demotion occurs. "
		"This number is given as seconds before the current date "
		"and time taken from the system clock divided by 1000. "
		"0 means use the current time of the current day. ";
	m->m_cgi   = "pqrdatem";
	m->m_off   = (char *)&cr.m_pqr_maxValDatedbDate - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "0"; 
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "demotion for pages based on proximity";
	m->m_desc  = "Demotion factor for proximity of query terms in "
		"a document.  The closer together terms occur in a "
		"document, the higher it will score."
		"0 means no demotion. ";
	m->m_cgi   = "pqrprox";
	m->m_scgi  = "pqrprox";
	m->m_sparm = 1;
	m->m_off   = (char *)&cr.m_pqr_demFactProximity - x;
	m->m_soff  = (char *)&si.m_pqr_demFactProximity - y;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "demotion for pages based on query terms section";
	m->m_desc  = "Demotion factor for where the query terms occur "
		"in the document.  If the terms only occur in a menu, "
		"a link, or a list, the document will be punished."
		"0 means no demotion. ";
	m->m_cgi   = "pqrinsec";
	m->m_scgi  = "pqrinsec";
	m->m_sparm = 1;
	m->m_off   = (char *)&cr.m_pqr_demFactInSection - x;
	m->m_soff  = (char *)&si.m_pqr_demFactInSection - y;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "weight of indexed score on pqr";
	m->m_desc  = "The proportion that the original score affects "
		"its rerank position. A factor of 1 will maintain "
		"the original score, 0 will only use the indexed "
		"score to break ties.";
	m->m_cgi   = "pqrorig";
	m->m_scgi  = "pqrorig";
	m->m_sparm = 1;
	m->m_off   = (char *)&cr.m_pqr_demFactOrigScore - x;
	m->m_soff  = (char *)&si.m_pqr_demFactOrigScore - y;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "1";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;



	m->m_title = "max value for demotion for pages based on proximity";
	m->m_desc  = "Max summary score where no more demotion occurs above. "
		"Pages with a summary score greater than or equal to this "
		"value will not be demoted. ";
	m->m_cgi   = "pqrproxm";
	m->m_off   = (char *)&cr.m_pqr_maxValProximity - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "100000"; 
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;


	m->m_title = "demotion for query being exclusivly in a subphrase";
	m->m_desc  = "Search result which contains the query terms only"
		" as a subphrase of a larger phrase will have its score "
		" reduced by this percent.";
	m->m_cgi   = "pqrspd";
	m->m_off   = (char *)&cr.m_pqr_demFactSubPhrase - x;
	m->m_soff  = (char *)&si.m_pqr_demFactSubPhrase - y;
	m->m_sparm = 1;
	m->m_scgi  = "pqrspd";
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "demotion based on common inlinks";
	m->m_desc  = "Based on the number of inlinks a search results has "
		"which are in common with another search result.";
	m->m_cgi   = "pqrcid";
	m->m_off   = (char *)&cr.m_pqr_demFactCommonInlinks - x;
	m->m_soff  = (char *)&si.m_pqr_demFactCommonInlinks - y;
	m->m_sparm = 1;
	m->m_scgi  = "pqrcid";
	m->m_type  = TYPE_FLOAT;
	m->m_def   = ".5";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "number of document calls multiplier";
	m->m_desc  = "Allows more results to be gathered in the case of "
		"an index having a high rate of duplicate results.  Generally"
		" expressed as 1.2";
	m->m_cgi   = "ndm";
	m->m_off   = (char *)&cr.m_numDocsMultiplier - x;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "1.2";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	/*
	m->m_title = "max documents to compute per host";
	m->m_desc  = "Limit number of documents to search that do not provide"
		" the required results.";
	m->m_cgi   = "mdi";
	m->m_off   = (char *)&cr.m_maxDocIdsToCompute - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "1000";
	m->m_group = 0;
	m++;
	*/

	m->m_title = "max real time inlinks";
	m->m_desc  = "Limit number of linksdb inlinks requested per result.";
	m->m_cgi   = "mrti";
	m->m_off   = (char *)&cr.m_maxRealTimeInlinks - x;
	m->m_soff  = (char *)&si.m_maxRealTimeInlinks - y;
	m->m_type  = TYPE_LONG;
	m->m_def   = "10000";
	m->m_group = 0;
	m->m_sparm = 1;
	m->m_scgi  = "mrti";
	m->m_smin  = 0;
	m->m_smax  = 100000;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "percent topic similar default";
	m->m_desc  = "Like above, but used for deciding when to cluster "
		"results by topic for the news collection.";
	m->m_cgi   = "ptcd";
	m->m_off   = (char *)&cr.m_topicSimilarCutoffDefault - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "50";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;	
	m++;

	//m->m_title = "max query terms";
	//m->m_desc  = "Do not allow more than this many query terms. Will "
	//	"return error in XML feed error tag if breeched.";
	//m->m_cgi   = "mqt";
	//m->m_off   = (char *)&cr.m_maxQueryTerms - x;
	//m->m_soff  = (char *)&si.m_maxQueryTerms - y;
	//m->m_type  = TYPE_LONG;
	//m->m_def   = "20"; // 20 for testing, normally 16
	//m->m_sparm = 1;
	//m->m_spriv = 1;
	//m++;

	/*
	m->m_title = "dictionary site";
	m->m_desc  = "Where do we send requests for definitions of search "
		"terms. Set to the empty string to turn this feature off.";
	m->m_cgi   = "dictionarySite";
	m->m_off   = (char *)&cr.m_dictionarySite - x;
	m->m_type  = TYPE_STRING;
	m->m_size  = SUMMARYHIGHLIGHTTAGMAXSIZE;
	m->m_def   = "http://www.answers.com/";
	m++;
	*/

	/*
	m->m_title = "allow links: searches";
	m->m_desc  = "Allows anyone access to perform links: searches on this "
		"collection.";
	m->m_cgi   = "als";
	m->m_off   = (char *)&cr.m_allowLinksSearch - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m++;
	*/
							    
	// REFERENCE PAGES CONTROLS
	m->m_title = "number of reference pages to generate";
	m->m_desc  = "What is the number of "
		"reference pages to generate per query? Set to 0 to save "
		"CPU time.";
	m->m_cgi   = "nrp";
	m->m_off   = (char *)&cr.m_refs_numToGenerate - x;
	m->m_soff  = (char *)&si.m_refs_numToGenerate - y;
	m->m_smaxc = (char *)&cr.m_refs_numToGenerateCeiling - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "0";
	m->m_priv  = 0;
	m->m_sparm = 1;
	m->m_smin  = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "number of reference pages to display";
	m->m_desc  = "What is the number of "
		"reference pages to display per query?";
	m->m_cgi   = "nrpdd";
	m->m_off   = (char *)&cr.m_refs_numToDisplay - x;
	m->m_soff  = (char *)&si.m_refs_numToDisplay - y;
	m->m_type  = TYPE_LONG;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_priv  = 0; // allow the (more) link
	m->m_sparm = 1;
	m->m_sprpg = 0; // do not propagate
        m->m_sprpp = 0; // do not propagate
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "docs to scan for reference pages";
	m->m_desc  = "How many search results should we "
		"scan for reference pages per query?";
	m->m_cgi   = "dsrp";
	m->m_off   = (char *)&cr.m_refs_docsToScan - x;
	m->m_soff  = (char *)&si.m_refs_docsToScan - y;
	m->m_smaxc = (char *)&cr.m_refs_docsToScanCeiling - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "30";
	m->m_group = 0;
	m->m_priv  = 0;
	m->m_sparm = 1;
	m->m_smin  = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "min references quality";
	m->m_desc  = "References with page quality below this "
		"will be excluded.  (set to 101 to disable references while "
		"still generating related pages.";
	m->m_cgi   = "mrpq";
	m->m_off   = (char *)&cr.m_refs_minQuality - x;
	m->m_soff  = (char *)&si.m_refs_minQuality - y;
	m->m_type  = TYPE_LONG;
	m->m_def   = "1";
	m->m_group = 0;
	m->m_priv  = 2;
	m->m_sparm = 1;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "min links per references";
	m->m_desc  = "References need this many links to results to "
		"be included.";
	m->m_cgi   = "mlpr";
	m->m_off   = (char *)&cr.m_refs_minLinksPerReference - x;
	m->m_soff  = (char *)&si.m_refs_minLinksPerReference - y;
	m->m_type  = TYPE_LONG;
	m->m_def   = "2";
	m->m_group = 0;
	m->m_priv  = 2;
	m->m_sparm = 1;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "max linkers to consider for references per page";
	m->m_desc  = "Stop processing referencing pages after hitting this "
		"limit.";
	m->m_cgi   = "mrpl";
	m->m_off   = (char *)&cr.m_refs_maxLinkers - x;
	m->m_soff  = (char *)&si.m_refs_maxLinkers - y;
	m->m_smaxc = (char *)&cr.m_refs_maxLinkersCeiling - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "500";
	m->m_group = 0;
	m->m_priv  = 2;
	m->m_sparm = 1;
	m->m_smin  = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "page fetch multiplier for references";
	m->m_desc  = "Use this multiplier to fetch more than the required "
                "number of reference pages.  fetches N * (this parm) "
		"references and displays the top scoring N.";
	m->m_cgi   = "ptrfr";
	m->m_off   = (char *)&cr.m_refs_additionalTRFetch - x;
	m->m_soff  = (char *)&si.m_refs_additionalTRFetch - y;
	m->m_smaxc = (char *)&cr.m_refs_additionalTRFetchCeiling - x;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "1.5";
	m->m_group = 0;
	m->m_priv  = 2;
	m->m_sparm = 1;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "number of links coefficient";
	m->m_desc  = "A in A * numLinks + B * quality + C * "
		"numLinks/totalLinks.";
        m->m_cgi   = "nlc";
	m->m_off   = (char *)&cr.m_refs_numLinksCoefficient - x;
	m->m_soff  = (char *)&si.m_refs_numLinksCoefficient - y;
	m->m_type  = TYPE_LONG;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_priv  = 2;
	m->m_sparm = 1;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "quality coefficient";
	m->m_desc  = "B in A * numLinks + B * quality + C * "
		"numLinks/totalLinks.";
	m->m_cgi   = "qc";
	m->m_off   = (char *)&cr.m_refs_qualityCoefficient - x;
	m->m_soff  = (char *)&si.m_refs_qualityCoefficient - y;
	m->m_type  = TYPE_LONG;
	m->m_def   = "1";
	m->m_group = 0;
	m->m_priv  = 2;
	m->m_sparm = 1;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "link density coefficient";
	m->m_desc  = "C in A * numLinks + B * quality + C * "
		"numLinks/totalLinks.";
	m->m_cgi   = "ldc";
	m->m_off   = (char *)&cr.m_refs_linkDensityCoefficient - x;
	m->m_soff  = (char *)&si.m_refs_linkDensityCoefficient - y;
	m->m_type  = TYPE_LONG;
	m->m_def   = "1000";
	m->m_group = 0;
	m->m_priv  = 2;
	m->m_sparm = 1;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "add or multipy quality times link density";
	m->m_desc  = "[+|*] in A * numLinks + B * quality [+|*]"
		" C * numLinks/totalLinks.";
	m->m_cgi   = "mrs";
	m->m_off   = (char *)&cr.m_refs_multiplyRefScore - x;
	m->m_soff  = (char *)&si.m_refs_multiplyRefScore - y;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_group = 0;
	m->m_priv  = 2;
	m->m_sparm = 1;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	// reference pages ceiling parameters
	m->m_title = "maximum allowed value for "
		"numReferences parameter";
	m->m_desc  = "maximum allowed value for "
		"numReferences parameter";
	m->m_cgi   = "nrpc";
	m->m_off   = (char *)&cr.m_refs_numToGenerateCeiling - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "100";
	m->m_group = 0;
	m->m_priv  = 2;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "maximum allowed value for "
		"docsToScanForReferences parameter";
	m->m_desc  = "maximum allowed value for "
		"docsToScanForReferences parameter";
	m->m_cgi   = "dsrpc";
	m->m_off   = (char *)&cr.m_refs_docsToScanCeiling - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "100";
	m->m_group = 0;
	m->m_priv  = 2;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "maximum allowed value for "
		"maxLinkers parameter";
	m->m_desc  = "maximum allowed value for "
		"maxLinkers parameter";
	m->m_cgi   = "mrplc";
	m->m_off   = (char *)&cr.m_refs_maxLinkersCeiling - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "5000";
	m->m_group = 0;
	m->m_priv  = 2;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "maximum allowed value for "
		"additionalTRFetch";
	m->m_desc  = "maximum allowed value for "
		"additionalTRFetch parameter";
	m->m_cgi   = "ptrfrc";
	m->m_off   = (char *)&cr.m_refs_additionalTRFetchCeiling - x;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "10";
	m->m_group = 0;
	m->m_priv  = 2;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	// related pages parameters
	m->m_title = "number of related pages to generate";
	m->m_desc  = "number of related pages to generate.";
	m->m_cgi   = "nrpg";
	m->m_off   = (char *)&cr.m_rp_numToGenerate - x;
	m->m_soff  = (char *)&si.m_rp_numToGenerate - y;
	m->m_smaxc = (char *)&cr.m_rp_numToGenerateCeiling - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "0";
	m->m_priv  = 0;
	m->m_sparm = 1;
	m->m_smin  = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "number of related pages to display";
	m->m_desc  = "number of related pages to display.";
	m->m_cgi   = "nrpd";
	m->m_off   = (char *)&cr.m_rp_numToDisplay - x;
	m->m_soff  = (char *)&si.m_rp_numToDisplay - y;
	m->m_type  = TYPE_LONG;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_priv  = 0; // allow the (more) link
	m->m_sparm = 1;
	m->m_sprpg = 0; // do not propagate
        m->m_sprpp = 0; // do not propagate
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "number of links to scan for related pages";
	m->m_desc  = "number of links per reference page to scan for related "
		"pages.";
	m->m_cgi   = "nlpd";
	m->m_off   = (char *)&cr.m_rp_numLinksPerDoc - x;
	m->m_soff  = (char *)&si.m_rp_numLinksPerDoc - y;
	m->m_smaxc = (char *)&cr.m_rp_numLinksPerDocCeiling - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "1024";
	m->m_group = 0;
	m->m_priv  = 2;
	m->m_sparm = 1;
	m->m_smin  = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "min related page quality";
	m->m_desc  = "related pages with a quality lower than this will be "
		"ignored.";
	m->m_cgi   = "merpq";
	m->m_off   = (char *)&cr.m_rp_minQuality - x;
	m->m_soff  = (char *)&si.m_rp_minQuality - y;
	m->m_type  = TYPE_LONG;
	m->m_def   = "30";
	m->m_group = 0;
	m->m_priv  = 2;
	m->m_sparm = 1;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "min related page score";
	m->m_desc  = "related pages with an adjusted score lower than this "
		"will be ignored.";
	m->m_cgi   = "merps";
	m->m_off   = (char *)&cr.m_rp_minScore - x;
	m->m_soff  = (char *)&si.m_rp_minScore - y;
	m->m_type  = TYPE_LONG;
	m->m_def   = "1";
	m->m_group = 0;
	m->m_priv  = 2;
	m->m_sparm = 1;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "min related page links";
	m->m_desc  = "related pages with less than this number of links"
		" will be ignored.";
	m->m_cgi   = "merpl";
	m->m_off   = (char *)&cr.m_rp_minLinks - x;
	m->m_soff  = (char *)&si.m_rp_minLinks - y;
	m->m_type  = TYPE_LONG;
	m->m_def   = "2";
	m->m_group = 0;
	m->m_priv  = 2;
	m->m_sparm = 1;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "coefficient for number of links in related pages score "
		"calculation";
	m->m_desc  = "A in A * numLinks + B * avgLnkrQlty + C * PgQlty"
		" + D * numSRPLinks.";
	m->m_cgi   = "nrplc";
	m->m_off   = (char *)&cr.m_rp_numLinksCoeff - x;
	m->m_soff  = (char *)&si.m_rp_numLinksCoeff - y;
	m->m_type  = TYPE_LONG;
	m->m_def   = "10";
	m->m_group = 0;
	m->m_priv  = 2;
	m->m_sparm = 1;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "coefficient for average linker quality in related pages "
		"score calculation";
	m->m_desc  = "B in A * numLinks + B * avgLnkrQlty + C * PgQlty"
		" + D * numSRPLinks.";
	m->m_cgi   = "arplqc";
	m->m_off   = (char *)&cr.m_rp_avgLnkrQualCoeff - x;
	m->m_soff  = (char *)&si.m_rp_avgLnkrQualCoeff - y;
	m->m_type  = TYPE_LONG;
	m->m_def   = "1";
	m->m_group = 0;
	m->m_priv  = 2;
	m->m_sparm = 1;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "coefficient for page quality in related pages "
		"score calculation";
	m->m_desc  = "C in A * numLinks + B * avgLnkrQlty + C * PgQlty"
		" + D * numSRPLinks";
	m->m_cgi   = "qrpc";
	m->m_off   = (char *)&cr.m_rp_qualCoeff - x;
	m->m_soff  = (char *)&si.m_rp_qualCoeff - y;
	m->m_type  = TYPE_LONG;
	m->m_def   = "1";
	m->m_group = 0;
	m->m_priv  = 2;
	m->m_sparm = 1;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "coefficient for search result links in related pages "
		"score calculation";
	m->m_desc  = "D in A * numLinks + B * avgLnkrQlty + C * PgQlty"
		" + D * numSRPLinks.";
	m->m_cgi   = "srprpc";
	m->m_off   = (char *)&cr.m_rp_srpLinkCoeff - x;
	m->m_soff  = (char *)&si.m_rp_srpLinkCoeff - y;
	m->m_type  = TYPE_LONG;
	m->m_def   = "1";
	m->m_group = 0;
	m->m_priv  = 2;
	m->m_sparm = 1;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "number of related page summary excerpts";
	m->m_desc  = "What is the maximum number of "
		"excerpts displayed in the summary of a related page?";
	m->m_cgi   = "nrps";
	m->m_off   = (char *)&cr.m_rp_numSummaryLines - x;
	m->m_soff  = (char *)&si.m_rp_numSummaryLines - y;
	m->m_smaxc = (char *)&cr.m_rp_numSummaryLinesCeiling - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "1";
	m->m_group = 0;
	m->m_priv  = 2;
	m->m_sparm = 1;
	m->m_smin  = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;


	m->m_title = "highlight query terms in related pages summary";
	m->m_desc  = "Highlight query terms in related pages summary.";
	m->m_cgi   = "hqtirps"; 
	m->m_off   = (char *)&cr.m_rp_doRelatedPageSumHighlight - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_priv  = 2;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;


	m->m_title = "number of characters to display in title before "
		"truncating";
	m->m_desc  = "Truncates a related page title after this many "
		"charaters and adds ...";
	m->m_cgi   = "ttl";
	m->m_off   = (char *)&cr.m_rp_titleTruncateLimit - x;
	m->m_soff  = (char *)&si.m_rp_titleTruncateLimit - y;
	m->m_type  = TYPE_LONG;
	m->m_def   = "50";
	m->m_group = 0;
	m->m_priv  = 2;
	m->m_sparm = 1;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "use results pages as references";
	m->m_desc  = "Use the search results' links in order to generate "
		"related pages.";
	m->m_cgi   = "urar"; 
	m->m_off   = (char *)&cr.m_rp_useResultsAsReferences - x;
	m->m_soff  = (char *)&si.m_rp_useResultsAsReferences - y;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_priv  = 2;
	m->m_sparm = 1;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "get related pages from other cluster";
	m->m_desc  = "Say yes here to make Gigablast check another Gigablast "
		"cluster for title rec for related pages. Gigablast will "
		"use the hosts2.conf file in the working directory to "
		"tell it what hosts belong to the other cluster.";
	m->m_cgi   = "erp"; // external related pages
	m->m_off   = (char *)&cr.m_rp_getExternalPages - x;
	m->m_soff  = (char *)&si.m_rp_getExternalPages - y;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_priv  = 2;
	m->m_sparm = 1;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "collection for other related pages cluster";
	m->m_desc  = "Gigablast will fetch the related pages title record "
		"from this collection in the other cluster.";
	m->m_cgi   = "erpc"; // external related pages collection
	m->m_off   = (char *)&cr.m_rp_externalColl - x;
	m->m_soff  = (char *)&si.m_rp_externalColl - y;
	m->m_type  = TYPE_STRING;
	m->m_size  = MAX_COLL_LEN;
	m->m_def   = "main";
	m->m_group = 0;
	m->m_priv  = 2;
	m->m_sparm = 1;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	// relate pages ceiling parameters
	m->m_title = "maximum allowed value for numToGenerate parameter";
	m->m_desc  = "maximum allowed value for numToGenerate parameter";
	m->m_cgi   = "nrpgc";
	m->m_off   = (char *)&cr.m_rp_numToGenerateCeiling - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "100";
	m->m_group = 0;
	m->m_priv  = 2;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "maximum allowed value for numRPLinksPerDoc parameter";
	m->m_desc  = "maximum allowed value for numRPLinksPerDoc parameter";
	m->m_cgi   = "nlpdc";
	m->m_off   = (char *)&cr.m_rp_numLinksPerDocCeiling - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "5000";
	m->m_group = 0;
	m->m_priv  = 2;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "maximum allowed value for numSummaryLines parameter";
	m->m_desc  = "maximum allowed value for numSummaryLines parameter";
	m->m_cgi   = "nrpsc";
	m->m_off   = (char *)&cr.m_rp_numSummaryLinesCeiling - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "10";
	m->m_group = 0;
	m->m_priv  = 2;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	// import search results controls
	m->m_title = "how many imported results should we insert";
	m->m_desc  = "Gigablast will import X search results from the "
		"external cluster given by hosts2.conf and merge those "
		"search results into the current set of search results. "
		"Set to 0 to disable.";
	m->m_cgi   = "imp";
	m->m_off   = (char *)&cr.m_numResultsToImport - x;
	m->m_soff  = (char *)&si.m_numResultsToImport - y;
	m->m_type  = TYPE_LONG;
	m->m_def   = "0";
	m->m_priv  = 2;
	m->m_sparm = 1;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "imported score weight";
	m->m_desc  = "The score of all imported results will be multiplied "
		"by this number. Since results are mostly imported from "
		"a large collection they will usually have higher scores "
		"because of having more link texts or whatever, so tone it "
		"down a bit to put it on par with the integrating collection.";
	m->m_cgi   = "impw";
	m->m_off   = (char *)&cr.m_importWeight - x;
	m->m_soff  = (char *)&si.m_importWeight - y;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = ".80";
	m->m_group = 0;
	m->m_priv  = 2;
	m->m_sparm = 1;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "how many linkers must each imported result have";
	m->m_desc  = "The urls of imported search results must be linked to "
		"by at least this many documents in the primary collection.";
	m->m_cgi   = "impl";
	m->m_off   = (char *)&cr.m_minLinkersPerImportedResult - x;
	m->m_soff  = (char *)&si.m_minLinkersPerImportedResult - y;
	m->m_type  = TYPE_LONG;
	m->m_def   = "3";
	m->m_group = 0;
	m->m_priv  = 2;
	m->m_sparm = 1;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "num linkers weight";
	m->m_desc  = "The number of linkers an imported result has from "
		"the base collection is multiplied by this weight and then "
		"added to the final score. The higher this is the more an "
		"imported result with a lot of linkers will be boosted. "
		"Currently, 100 is the max number of linkers permitted.";
	m->m_cgi   = "impnlw";
	m->m_off   = (char *)&cr.m_numLinkerWeight - x;
	m->m_soff  = (char *)&si.m_numLinkerWeight - y;
	m->m_type  = TYPE_LONG;
	m->m_def   = "50";
	m->m_group = 0;
	m->m_priv  = 2;
	m->m_sparm = 1;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "the name of the collection to import from";
	m->m_desc  = "Gigablast will import X search results from this "
		"external collection and merge them into the current search "
		"results.";
	m->m_cgi   = "impc";
	m->m_off   = (char *)&cr.m_importColl - x;
	m->m_soff  = (char *)&si.m_importColl - y;
	m->m_type  = TYPE_STRING;
	m->m_size  = MAX_COLL_LEN;
	m->m_def   = "main";
	m->m_group = 0;
	m->m_priv  = 2;
	m->m_sparm = 1;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "max similar results for cluster by topic";
	m->m_desc  = "Max similar results to show when clustering by topic.";
	m->m_cgi   = "ncbt";
	m->m_off   = (char *)&cr.m_maxClusterByTopicResults - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "10";
	m->m_group = 0;
	m->m_sparm = 1;
	m->m_scgi  = "ncbt";
	m->m_soff  = (char *)&si.m_maxClusterByTopicResults - y;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "number of extra results to get for cluster by topic";
	m->m_desc  = "number of extra results to get for cluster by topic";
	m->m_cgi   = "ntwo";
	m->m_off   = (char *)&cr.m_numExtraClusterByTopicResults - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "100";
	m->m_group = 0;
	m->m_sparm = 1;
	m->m_scgi  = "ntwo";
	m->m_soff  = (char *)&si.m_numExtraClusterByTopicResults - y;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;


	m->m_title = "Minimum number of in linkers required to consider getting"
		" the title from in linkers";
	m->m_desc  = "Minimum number of in linkers required to consider getting"
	       "	the title from in linkers";
	m->m_cgi   = "mininlinkers";
	m->m_off   = (char *)&cr.m_minTitleInLinkers - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "10";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "Max number of in linkers to consider";
	m->m_desc  = "Max number of in linkers to consider for getting in "
		"linkers titles.";
	m->m_cgi   = "maxinlinkers";
	m->m_off   = (char *)&cr.m_maxTitleInLinkers - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "128";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	/*
	m->m_title = "use new summary generator";
	m->m_desc  = "Also used for gigabits and titles.";
	m->m_cgi   = "uns"; // external related pages
	m->m_off   = (char *)&cr.m_useNewSummaries - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_sparm = 1;
	m->m_scgi  = "uns";
	m->m_soff  = (char *)&si.m_useNewSummaries - y;
	m++;
	*/

	m->m_title = "summary mode";
	m->m_desc  = "0 = old compatibility mode, 1 = UTF-8 mode, "
		"2 = fast ASCII mode, "
		"3 = Ascii Proximity Summary, "
		"4 = Utf8 Proximity Summary, "
		"5 = Ascii Pre Proximity Summary, "
		"6 = Utf8 Pre Proximity Summary:";
	m->m_cgi   = "smd";
	m->m_off   = (char *)&cr.m_summaryMode - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "0";
	m->m_sparm = 1;
	m->m_scgi  = "smd";
	m->m_soff  = (char*) &si.m_summaryMode - y;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "max summary len";
	m->m_desc  = "What is the maximum number of "
		"characters displayed in a summary for a search result?";
	m->m_cgi   = "sml";
	m->m_off   = (char *)&cr.m_summaryMaxLen - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "512";
	m++;

	m->m_title = "max summary excerpts";
	m->m_desc  = "What is the maximum number of "
		"excerpts displayed in the summary of a search result?";
	m->m_cgi   = "smnl";
	m->m_off   = (char *)&cr.m_summaryMaxNumLines - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "4";
	m->m_group = 0;
	m++;

	m->m_title = "max summary excerpt length";
	m->m_desc = "What is the maximum number of "
		"characters allowed per summary excerpt?";
	m->m_cgi   = "smxcpl";
	m->m_off   = (char *)&cr.m_summaryMaxNumCharsPerLine - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "90";
	m->m_group = 0;
	m++;

	m->m_title = "default number of summary excerpts";
	m->m_desc  = "What is the default number of "
		"summary excerpts displayed per search result?";
	m->m_cgi   = "sdnl";
	m->m_off   = (char *)&cr.m_summaryDefaultNumLines - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "3";
	m->m_group = 0;
	m->m_sparm = 1;
	m->m_scgi  = "ns";
	m->m_soff  = (char *)&si.m_numLinesInSummary - y;
	m++;

	m->m_title = "max summary line width";
	m->m_desc  = "<br> tags are inserted to keep the number "
		"of chars in the summary per line at or below this width. "
		"Strings without spaces that exceed this "
		"width are not split.";
	m->m_cgi   = "smw";
	m->m_off   = (char *)&cr.m_summaryMaxWidth - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "80";
	m->m_group = 0;
	m->m_sparm = 1;
	m->m_scgi  = "sw";
	m->m_soff  = (char *)&si.m_summaryMaxWidth - y;
	m++;

	m->m_title = "bytes of doc to scan for summary generation";
	m->m_desc  = "Truncating this will miss out on good summaries, but "
		"performance will increase.";
	m->m_cgi   = "clmfs";
	m->m_off   = (char *)&cr.m_contentLenMaxForSummary - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "70000";
	m->m_group = 0;
	m++;       

	m->m_title = "Prox summary carver radius";
	m->m_desc  = "Maximum number of characters to allow in between "
		"search terms.";
	m->m_cgi   = "pscr";
	m->m_off   = (char *)&cr.m_proxCarveRadius - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "256";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "front highlight tag";
	m->m_desc  = "Front html tag used for highlightig query terms in the "
		"summaries displated in the search results.";
	m->m_cgi   = "sfht";
	m->m_off   = (char *)cr.m_summaryFrontHighlightTag - x;
	m->m_type  = TYPE_STRING;
	m->m_size  = SUMMARYHIGHLIGHTTAGMAXSIZE ;
	m->m_def   = "<b style=\"color:black;background-color:#ffff66\">";
	m->m_group = 0;
	m++;

	m->m_title = "back highlight tag";
	m->m_desc  = "Front html tag used for highlightig query terms in the "
		"summaries displated in the search results.";
	m->m_cgi   = "sbht";
	m->m_off   = (char *)cr.m_summaryBackHighlightTag - x;
	m->m_type  = TYPE_STRING;
	m->m_size  = SUMMARYHIGHLIGHTTAGMAXSIZE ;
	m->m_def   = "</b>";
	m->m_group = 0;
	m++;

	/*
	m->m_title = "enable page turk";
	m->m_desc  = "If enabled, search results shall feed the page turk "
		"is used to mechanically rank websites.";
	m->m_cgi   = "ept";
	m->m_def   = "0";
	m->m_off   = (char *)&cr.m_pageTurkEnabled - x;
	m->m_type  = TYPE_BOOL;
	m++;
	*/

	m->m_title = "docs to scan for topics";
	m->m_desc  = "How many search results should we "
		"scan for related topics (gigabits) per query?";
	m->m_cgi   = "dsrt";
	m->m_off   = (char *)&cr.m_docsToScanForTopics - x;
	m->m_soff  = (char *)&si.m_docsToScanForTopics - y;
	m->m_type  = TYPE_LONG;
	m->m_def   = "300";
	m->m_sparm = 1;
	m++;

	m->m_title = "ip restriction for topics";
	m->m_desc  = "Should Gigablast only get one document per IP domain "
		"and per domain for topic (gigabit) generation?";
	m->m_cgi   = "ipr";
	m->m_off   = (char *)&cr.m_ipRestrict - x;
	m->m_soff  = (char *)&si.m_ipRestrictForTopics - y;
	m->m_type  = TYPE_BOOL;
	// default to 0 since newspaperarchive only has docs from same IP dom
	m->m_def   = "0";
	m->m_sparm = 1;
	m->m_group = 0;
	m++;

	m->m_title = "remove overlapping topics";
	m->m_desc  = "Should Gigablast remove overlapping topics (gigabits)?";
	m->m_cgi   = "rot";
	m->m_off   = (char *)&cr.m_topicRemoveOverlaps - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_group = 0;
	m++;

	m->m_title = "number of related topics";
	m->m_desc  = "What is the number of "
		"related topics (gigabits) "
		"displayed per query? Set to 0 to save "
		"CPU time.";
	m->m_cgi   = "nrt";
	m->m_off   = (char *)&cr.m_numTopics - x;
	m->m_soff  = (char *)&si.m_numTopicsToDisplay - y;
	m->m_type  = TYPE_LONG;
	m->m_def   = "11";
	m->m_group = 0;
	m->m_sparm = 1;
	m->m_sprpg = 0; // do not propagate
        m->m_sprpp = 0; // do not propagate
	m++;

	m->m_title = "min topics score";
	m->m_desc  = "Related topics (gigabits) with scores below this "
		"will be excluded. Scores range from 0% to over 100%.";
	m->m_cgi   = "mts";
	m->m_off   = (char *)&cr.m_minTopicScore - x;
	m->m_soff  = (char *)&si.m_minTopicScore - y;
	m->m_type  = TYPE_LONG;
	m->m_def   = "5";
	m->m_group = 0;
	m->m_sparm = 1;
	m++;

	m->m_title = "min topic doc count";
	m->m_desc  = "How many documents must contain the topic (gigabit) "
		"for it to be displayed.";
	m->m_cgi   = "mdc";
	m->m_off   = (char *)&cr.m_minDocCount - x;
	m->m_soff  = (char *)&si.m_minDocCount - y;
	m->m_type  = TYPE_LONG;
	m->m_def   = "2";
	m->m_group = 0;
	m->m_sparm = 1;
	m++;

	m->m_title = "dedup doc percent for topics";
	m->m_desc  = "If a document is this percent similar to another "
		"document with a higher score, then it will not contribute "
		"to the topic (gigabit) generation.";
	m->m_cgi   = "dsp";
	m->m_off   = (char *)&cr.m_dedupSamplePercent - x;
	m->m_soff  = (char *)&si.m_dedupSamplePercent - y;
	m->m_type  = TYPE_LONG;
	m->m_def   = "80";
	m->m_group = 0;
	m->m_sparm = 1;
	m++;

	m->m_title = "max words per topic";
	m->m_desc  = "Maximum number of words a topic (gigabit) can have. "
		"Affects "
		"raw feeds, too.";
	m->m_cgi   = "mwpt";
	m->m_off   = (char *)&cr.m_maxWordsPerTopic - x;
	m->m_soff  = (char *)&si.m_maxWordsPerTopic - y;
	m->m_type  = TYPE_LONG;
	m->m_def   = "6";
	m->m_group = 0;
	m->m_sparm = 1;
	m++;

	m->m_title = "topic max sample size";
	m->m_desc  = "Max chars to sample from each doc for topics "
		"(gigabits).";
	m->m_cgi   = "tmss";
	m->m_off   = (char *)&cr.m_topicSampleSize - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "4096";
	m->m_group = 0;
	m++;

	m->m_title = "topic max punct len";
	m->m_desc  = "Max sequential punct chars allowed in a topic (gigabit)."
		" Set to 1 for speed, 5 or more for best topics but twice as "
		"slow.";
	m->m_cgi   = "tmpl";
	m->m_off   = (char *)&cr.m_topicMaxPunctLen - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "1";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;


	m->m_title = "display dmoz categories in results";
	m->m_desc  = "If enabled, results in dmoz will display their "
		"categories on the results page.";
	m->m_cgi   = "ddc";
	m->m_off   = (char *)&cr.m_displayDmozCategories - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m++;

	m->m_title = "display indirect dmoz categories in results";
	m->m_desc  = "If enabled, results in dmoz will display their "
		"indirect categories on the results page.";
	m->m_cgi   = "didc";
	m->m_off   = (char *)&cr.m_displayIndirectDmozCategories - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_group = 0;
	m++;

	m->m_title = "display Search Category link to query category of result";
	m->m_desc  = "If enabled, a link will appear next to each category "
		"on each result allowing the user to perform their query "
		"on that entire category.";
	m->m_cgi   = "dscl";
	m->m_off   = (char *)&cr.m_displaySearchCategoryLink - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_group = 0;
	m++;

	m->m_title = "use dmoz for untitled";
	m->m_desc  = "Yes to use DMOZ given title when a page is untitled but "
		     "is in DMOZ.";
	m->m_cgi   = "udfu";
	m->m_off   = (char *)&cr.m_useDmozForUntitled - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_group = 0;
	m++;

	m->m_title = "show dmoz summaries";
	m->m_desc  = "Yes to always show DMOZ summaries with search results "
		     "that are in DMOZ.";
	m->m_cgi   = "udsm";
	m->m_off   = (char *)&cr.m_showDmozSummary - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_group = 0;
	m++;

	m->m_title = "show adult category on top";
	m->m_desc  = "Yes to display the Adult category in the Top category";
	m->m_cgi   = "sacot";
	m->m_off   = (char *)&cr.m_showAdultCategoryOnTop - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_group = 0;
	m++;

	/*
	m->m_title = "show sensitive info in xml feed";
	m->m_desc  = "If enabled, we show certain tagb tags for each "
		"search result, allow &amp;inlinks=1 cgi parms, show "
		"<docsInColl>, etc. in the xml feed. Created for buzzlogic.";
	m->m_cgi   = "sss";
	m->m_off   = (char *)&cr.m_showSensitiveStuff - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m++;
	*/

	m->m_title = "display indexed date";
	m->m_desc  = "Display the indexed date along with results.";
	m->m_cgi   = "didt";
	m->m_off   = (char *)&cr.m_displayIndexedDate - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "display last modified date";
	m->m_desc  = "Display the last modified date along with results.";
	m->m_cgi   = "dlmdt";
	m->m_off   = (char *)&cr.m_displayLastModDate - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "display published date";
	m->m_desc  = "Display the published date along with results.";
	m->m_cgi   = "dipt";
	m->m_off   = (char *)&cr.m_displayPublishDate - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "enable click 'n' scroll";
	m->m_desc  = "The [cached] link on results pages loads click n "
		"scroll.";
	m->m_cgi   = "ecns";
	m->m_off   = (char *)&cr.m_clickNScrollEnabled - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

        m->m_title = "use data feed account server";
        m->m_desc  = "Enable/disable the use of a remote account verification "
                "for Data Feed Customers.";
        m->m_cgi   = "dfuas";
        m->m_off   = (char *)&cr.m_useDFAcctServer - x;
        m->m_type  = TYPE_BOOL;
        m->m_def   = "0";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
        m++;

        m->m_title = "data feed server ip";
        m->m_desc  = "The ip address of the Gigablast data feed server to "
                "retrieve customer account information from.";
        m->m_cgi   = "dfip";
        m->m_off   = (char *)&cr.m_dfAcctIp - x;
        m->m_type  = TYPE_IP;
        m->m_def   = "2130706433";
        m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
        m++;

        m->m_title = "data feed server port";
        m->m_desc  = "The port of the Gigablast data feed server to retrieve "
                "customer account information from.";
        m->m_cgi   = "dfport";
        m->m_off   = (char *)&cr.m_dfAcctPort - x;
        m->m_type  = TYPE_LONG;
        m->m_def   = "8040";
        m->m_group = 0;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
        m++;

	/*
        m->m_title = "data feed server collection";
        m->m_desc  = "The collection on the Gigablast data feed server to "
                "retrieve customer account information from.";
        m->m_cgi   = "dfcoll";
        m->m_off   = (char *)&cr.m_dfAcctColl - x;
        m->m_type  = TYPE_STRING;
        m->m_size  = MAX_COLL_LEN;
        m->m_def   = "customers";
        m->m_group = 0;
        m++;
	*/

	//
	// not sure cols=x goes here or not
	//
	/*
	m->m_title = "Number Of Columns(1-6)";
	m->m_desc  = "How many columns results should be shown in. (1-6)";
	m->m_cgi   = "cols";
	m->m_smin  = 1;
	m->m_smax  = 6;
	m->m_off   = (char *)&cr.m_numCols - x;
	m->m_soff  = (char *)&si.m_numCols - y;
	m->m_type  = TYPE_LONG;
	m->m_def   = "1";
	m->m_group = 0;
	m->m_sparm = 1;
	m++;
	*/

	//
	// Gets the screen width
	//
	/*
	m->m_title = "Screen Width";
	m->m_desc  = "screen size of browser window";
	m->m_cgi   = "ws";
	m->m_smin  = 600;
	m->m_off   = (char *)&cr.m_screenWidth - x;
	m->m_soff  = (char *)&si.m_screenWidth - y;
	m->m_type  = TYPE_LONG;
	m->m_def   = "1100";
	m->m_group = 0;
	m->m_sparm = 1;
	m++;
	*/

	/*
	m->m_title = "collection hostname";
	m->m_desc  = "Hostname that will default to this collection. Blank"
		     " for none or default collection.";
	m->m_cgi   = "chstn";
	m->m_off   = (char *)cr.m_collectionHostname - x;
	m->m_type  = TYPE_STRING;
	m->m_size  = MAX_URL_LEN;
	m->m_def   = "";
	m++;

	m->m_title = "collection hostname (1)";
	m->m_desc  = "Hostname that will default to this collection. Blank"
		     " for none or default collection.";
	m->m_cgi   = "chstna";
	m->m_off   = (char *)cr.m_collectionHostname1 - x;
	m->m_type  = TYPE_STRING;
	m->m_size  = MAX_URL_LEN;
	m->m_def   = "";
	m->m_group = 0;
	m++;

	m->m_title = "collection hostname (2)";
	m->m_desc  = "Hostname that will default to this collection. Blank"
		     " for none or default collection.";
	m->m_cgi   = "chstnb";
	m->m_off   = (char *)cr.m_collectionHostname2 - x;
	m->m_type  = TYPE_STRING;
	m->m_size  = MAX_URL_LEN;
	m->m_def   = "";
	m->m_group = 0;
	m++;
	*/

	/*
	m->m_title = "html head";
	m->m_desc  = "Html to display before the search results. Convenient "
		"for changing colors and displaying logos. Use the variable, "
		"%q, to represent the query to display in a text box. "
		"Use %e to display it in a url.  "
		"Use %e to print the page encoding.Use %D to print a drop down "
		"menu for the number of search results to return. Use %S "
		"to print sort by date or relevance link. Use %L to "
		"display the logo. Use %R to display radio buttons for site "
		"search. Use %F to begin the form. and use %H to insert "
		"hidden text "
		"boxes of parameters, both %F and %H are necessary. "
		"Use %f to display "
		"the family filter radio buttons. "
		"Directory: Use %s to display the directory "
		"search type options. Use %l to specify the location of "
		"dir=rtl in the body tag for RTL pages. "
		"Use %where and %when to substitute the where and when of "
		"the query. These values may be set based on the cookie if "
		"none was explicitly given. "
		"IMPORTANT: In the xml configuration file, this html "
		"must be encoded (less thans mapped to &lt;, etc.).";
	m->m_cgi   = "hh";
	m->m_off   = (char *)cr.m_htmlHead - x;
	m->m_plen  = (char *)&cr.m_htmlHeadLen - x; // length of string
	m->m_type  = TYPE_STRINGBOX;
	m->m_size  = MAX_HTML_LEN + 1;
	m->m_def   = 
		"<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 "
		"Transitional//EN\">\n"
		"<html>\n"
		"<head>\n"
		"<title>Gigablast Search Results</title>\n"
		"<meta http-equiv=\"Content-Type\" "
		"content=\"text/html; charset=utf-8\">\n"
		"<style><!--\n"
		"body {\n"
		"font-family:Arial, Helvetica, sans-serif;\n"
		"color: #000000;\n"
		"font-size: 12px;\n"
		"margin: 20px 5px;\n"
		"}\n"
		"a:link {color:#00c}\n"
		"a:visited {color:#551a8b}\n"
		"a:active {color:#f00}\n"
		".bold {font-weight: bold;}\n"
		".bluetable {background:#d1e1ff;margin-bottom:15px;"
		"font-size:12px;}\n"
		".url {color:#008000;}\n"
		".cached, .cached a {font-size: 10px;color: #666666;\n"
		"}\n"
		"table {\n"
		"font-family:Arial, Helvetica, sans-serif;\n"
		"color: #000000;\n"
		"font-size: 12px;\n"
		"}\n"
		".directory {font-size: 16px;}\n"
		"-->\n"
		"</style>\n"
		"</head>\n"
		"<body%l>\n"

		//"<form method=\"get\" action=\"/search\" name=\"f\">\n"
		// . %F prints the <form method=...> tag
		// . method will be GET or POST depending on the size of the
		//   input data. MSIE can't handle sending large GETs requests
		//   that are more than like 1k or so, which happens a lot with
		//   our CTS technology (the sites= cgi parm can be very large)
		"%F"
		"<table cellpadding=\"2\" cellspacing=\"0\" border=\"0\">\n"
		"<tr>\n"
		"<td valign=top>"
		// this prints the Logo
		"%L"
		//"<a href=\"/\">"
		//"<img src=\"logo2.gif\" alt=\"Gigablast Logo\" "
		//"width=\"210\" height=\"25\" border=\"0\" valign=\"top\">"
		//"</a>"
		"</td>\n"

		"<td valign=top>\n"
		"<nobr>\n"
		"<input type=\"text\" name=\"q\" size=\"60\" value=\"\%q\"> " 
		// %D is the number of results drop down menu
		"\%D" 
		"<input type=\"submit\" value=\"Blast It!\" border=\"0\">\n"
		"</nobr>\n"
		// family filter
		// %R radio button for site(s) search
		"<br>%f %R\n"
		// directory search options
		"</td><td>%s</td>\n"
		"</tr>\n"
		"</table>\n"
		// %H prints the hidden for vars. Print them *after* the input 
		// text boxes, radio buttons, etc. so these hidden vars can be 
		// overriden as they should be.
		"%H"; 
	m->m_sparm = 1;
	m->m_soff  = (char *)&si.m_htmlHead - y;
	m++;

	m->m_title = "html tail";
	m->m_desc  = "Html to display after the search results.";
	m->m_cgi   = "ht";
	m->m_off   = (char *)cr.m_htmlTail - x;
	m->m_plen  = (char *)&cr.m_htmlTailLen - x; // length of string
	m->m_type  = TYPE_STRINGBOX;
	m->m_size  = MAX_HTML_LEN + 1;
	m->m_def   = 
		"<br>\n"
		"%F<table cellpadding=2 cellspacing=0 border=0>\n"
		"<tr><td></td>\n"
		"<td valign=top align=center>\n"
		"<nobr>"
		"<input type=text name=q size=60 value=\"%q\"> %D\n"
		"<input type=submit value=\"Blast It!\" border=0>\n"
		"</nobr>"
		// family filter
		"<br>%f %R\n"
		"</td><td>%s</td>\n"
		"</tr>\n"
		"</table>\n"
		"Try your search on  \n"
		"<a href=http://www.google.com/search?q=%e>google</a> &nbsp;\n"
		"<a href=http://search.yahoo.com/bin/search?p=%e>yahoo</a> "
		"&nbsp;\n"
		//"<a href=http://www.alltheweb.com/search?query=%e>alltheweb"
		//"</a>\n"
		"<a href=http://search.dmoz.org/cgi-bin/search?search=%e>"
		"dmoz</a> &nbsp;\n"
		//"<a href=http://search01.altavista.com/web/results?q=%e>"
		//"alta vista</a>\n"
		"<a href=http://s.teoma.com/search?q=%e>teoma</a> &nbsp;\n"
		"<a href=http://wisenut.com/search/query.dll?q=%e>wisenut"
		"</a>\n"
		"</font></body>\n";
	//m->m_def   = "</font></body></html>";
	m->m_group = 0;
	m->m_sparm = 1;
	m->m_soff  = (char *)&si.m_htmlTail - y;
	m++;

	m->m_title = "home page";
	m->m_desc  = "Html to display for the home page. Use %N for total "
		"number of pages indexed. Use %n for number of pages indexed "
		"for the current collection. "
		"Use %H so Gigablast knows where to insert "
		"the hidden form input tags, which must be there. Use %T to "
		"display the standard footer and %q to display the query in "
		"a text box. Use %t to display the directory TOP.";
	m->m_cgi   = "hp";
	m->m_off   = (char *)cr.m_htmlRoot - x;
	m->m_plen  = (char *)&cr.m_htmlRootLen - x; // length of string
	m->m_type  = TYPE_STRINGBOX;
	m->m_size  = MAX_HTML_LEN + 1;
	m->m_def   = 
		"<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 "
		"Transitional//EN\">\n"
		"<html>\n"
		"<head>\n"
		"<title>Gigablast</title>\n"
		"<meta http-equiv=\"Content-Type\" content=\"text/html; "
		"charset=utf-8\">\n"
		"<meta name=\"description\" content=\"A powerful, new "
		"search engine that does real-time indexing.\">\n"
		"<meta name=\"keywords\" content=\"search, search engine, "
		"search engines, search the web, fresh index\">\n"
		"<style type=\"text/css\">\n"
		"<!--\n"
		"body {\n"
		"font-family: Arial, Helvetica, sans-serif;\n"
		"background: #FFFFFF;\n"
		"font-size: 16px;\n"
		"color: #000000;\n"
		"text-align: center;\n"
		"margin: 20px 5px 20px;\n"
		"}\n"
		"a.search {\n"
		"font-weight: bold;\n"
		"color: #FFFFFF;\n"
		"text-decoration: underline;\n"
		"font-size: small;\n"
		"}\n"
		".redtop {\n"
		"color: #c62939;\n"
		"font-weight: bold;\n"
		"margin-top: 1.25em;\n"
		"margin-bottom: 1.15em;\n"
		"}\n"
		".red, .red a {\n"
		"color: #c62939;\n"
		"font-weight: bold;\n"
		"margin-top: 1.5em;\n"
		"margin-bottom: 2em;\n"
		"}\n"
		".nav, .nav a {\n"
		"color: #000000;\n"
		"font-weight: bold;\n"
		"margin-top: 3em;\n"
		"font-size: 96%;\n"
		"}\n"
		"-->\n"
		"</style>\n"
		"</head>\n"
		
		"<script>\n"
		"<!--\n"
		"function x(){document.f.q.focus();}\n"
		"// --></script>\n"
		"<body onload=\"x()\">\n"
		"<a href=\"/\"><img src=\"logo.gif\" "
		"alt=\"Gigablast\" border=0></a>\n"
		"<p class=\"redtop\">Information Acceleration.</p>\n"

		"<form method=\"get\" action=\"/search\" name=\"f\">\n"
		"%H\n"
		"<table bgcolor=\"#0079ba\" border=0 cellpadding=6 "
		"width=100%>\n"
		"<tbody>\n"
		"<tr>\n"

		"<td width=50%>&nbsp;</td>\n"
		"<td width=60> <div align=\"center\">\n"
		"<input name=\"q\" value=\"%q\" size=60 type=\"text\"> \n" 
		"</td><td width=50%>\n"

		// %D is the drop down menu for # of search results
		"%%D &nbsp; "
		"<input value=\"Blast It!\" border=0 type=\"submit\"> <a "
		"href=\"/adv.html\" class=\"search\">"
		"<nobr>Advanced Search</nobr></a>\n"
		"</td>\n"
		"</tr>\n"
		"</tbody>\n"
		"</table>\n"
		"</form>\n"
		"<p style=\"margin-top: 1.5em;margin-bottom: 2.5em;\"><b>"
		"%N pages indexed</b></p>\n"
		"<p class=\"red\"><a href=\"/"
		"searchfeed.html\">XML Search Feed (new)</a></p>\n"
		
		"<p class=\"red\"><a href=\"http://sitesearch.gigablast.com/"
		"sitesearch.html\">Dedicated Site Search (new)</a></p>\n"
		"<p class=\"red\"><a href=\"/cts."
		"html\">Custom Topic Search (new)</a></p>\n"
		"<p class=\"red\"><a href=\"/ask."
		"html\">Gigablast Answers Questions</a></p>\n"
		"%T\n"
		"</body>\n"
		"</html>\n";
	m++;
	*/

	///////////////////////////////////////////
	// ACCESS CONTROLS
	///////////////////////////////////////////

	/*
	// ARRAYS
	// each will have its own table, title will be in first row
	// of that table, 2nd row is description, then one row per
	// element in the array, then a final row for adding new elements
	// if not exceeding our m->m_max limit.
	m->m_title = "Passwords Required to Search this Collection";
	m->m_desc  ="Passwords allowed to perform searches on this collection."
		" If no passwords are specified, then anyone can search it.";
	m->m_cgi   = "searchpwd";
	m->m_xml   = "searchPassword";
	m->m_max   = MAX_SEARCH_PASSWORDS;
	m->m_off   = (char *)cr.m_searchPwds - x;
	m->m_type  = TYPE_STRINGNONEMPTY;
	m->m_size  = PASSWORD_MAX_LEN+1; // string size max
	m->m_page  = PAGE_ACCESS;
	m->m_def   = "";
	m++;

	m->m_title = "IPs Banned from Searching this Collection";
	m->m_desc  = "These IPs are not allowed to search this collection or "
		"use add url. Useful to keep out miscreants. Use zero for the "
		"last number of the IP to ban an entire IP domain.";
	m->m_cgi   = "bip";
	m->m_xml   = "bannedIp";
	m->m_max   = MAX_BANNED_IPS;
	m->m_off   = (char *)cr.m_banIps - x;
	m->m_type  = TYPE_IP;
	m->m_def   = "";
	m++;

	m->m_title = "Only These IPs can Search this Collection";
	m->m_desc  = "Only these IPs are allowed to search the collection and "
		"use the add url facilities. If you'd like to make your "
		"collection publically searchable then do not add any IPs "
		"here.Use zero for the "
		"last number of the IP to restrict to an entire "
		"IP domain, i.e. 1.2.3.0.";
	m->m_cgi   = "searchip";
	m->m_xml   = "searchIp";
	m->m_max   = MAX_SEARCH_IPS;
	m->m_off   = (char *)cr.m_searchIps - x;
	m->m_type  = TYPE_IP;
	m->m_def   = "";
	m++;

	m->m_title = "Spam Assassin IPs";
	m->m_desc  = "Browsers coming from these IPs are deemed to be spam "
		  "assassins and have access to a subset of the controls to "
		"ban and remove domains and IPs from the index.";
	m->m_cgi   = "assip";
	m->m_xml   = "assassinIp";
	m->m_max   = MAX_SPAM_IPS;
	m->m_off   = (char *)cr.m_spamIps - x;
	m->m_type  = TYPE_IP;
	m->m_def   = "";
	m++;

	m->m_title = "Admin Passwords";
	m->m_desc  = "Passwords allowed to edit this collection record. "
		"First password can only be deleted by the master "
		"administrator. If no password of Admin IP is given at time "
		"of creation then the default password of 'footbar23' will "
		"be assigned.";
	m->m_cgi   = "apwd";
	m->m_xml   = "adminPassword";
	m->m_max   = MAX_ADMIN_PASSWORDS;
	m->m_off   = (char *)cr.m_adminPwds - x;
	m->m_type  = TYPE_STRINGNONEMPTY;
	m->m_size  = PASSWORD_MAX_LEN+1;
	m->m_def   = "";
	m++;

	m->m_title = "Admin IPs";
	m->m_desc  = "If someone connects from one of these IPs and provides "
		"a password from the table above then they will have full "
		"administrative priviledges for this collection. If you "
		"specified no Admin Passwords above then they need only "
		"connect from an IP in this table to get the privledges. ";
	m->m_cgi   = "adminip";
	m->m_xml   = "adminIp";
	m->m_max   = MAX_ADMIN_IPS;
	m->m_off   = (char *)cr.m_adminIps - x;
	m->m_type  = TYPE_IP;
	m->m_def   = "";
	m++;
	*/

	///////////////////////////////////////////
	// URL FILTERS
	///////////////////////////////////////////

	//m->m_title = "Url Filters";
	// this is description just for the conf file.
	//m->m_cdesc = "See overview.html for a description of URL filters.";
	//m->m_type  = TYPE_COMMENT;
	//m++;

	m->m_title = "expression";
	m->m_desc  = "Before downloading the contents of a URL, Gigablast "
		"first chains down this "
		"list of "
		"expressions</a>, "
		"starting with expression #0.  "
		//"This table is also consulted "
		//"for every outlink added to spiderdb. "
		"The first expression it matches is the ONE AND ONLY "
		"matching row for that url. "
		"It then uses the "
		//"<a href=/overview.html#spiderfreq>"
		"respider frequency, "
		//"<a href=/overview.html#spiderpriority>"
		"spider priority, etc. on the MATCHING ROW when spidering "
		//"and <a href=/overview.html#ruleset>ruleset</a> to "
		"that URL. "
		"If you specify the <i>expression</i> as "
		"<i><b>default</b></i> then that MATCHES ALL URLs. "
		"URLs with high spider priorities take spidering "
		"precedence over "
		"URLs with lower spider priorities. "
		"The respider frequency dictates how often a URL will "
		"be respidered. "

		"See the help table below for examples of all the supported "
		"expressions. "
		"Use the <i>&&</i> operator to string multiple expressions "
		"together in the same expression text box. "
		"A <i>spider priority</i> of <i>FILTERED</i> or <i>BANNED</i> "
		"will cause the URL to not be spidered, or if it has already "
		"been indexed, it will be deleted when it is respidered."
		"<br><br>";
		
		/*
		"A URL is respidered according to the "
		"spider frequency. If this is blank then Gigablast will "
		"use the spider frequency explicitly dictated by the rule "
		"set. If the ruleset does not contain a <spiderFrequency> "
		"xml tag, then Gigablast will "
		"intelligently determine the best time to respider that "
		"URL.<br><br>"
		
		"If the "
		"<a href=/overview.html#spiderpriority>"
		"spider priority</a> of a URL is undefined then "
		"Gigablast will use the spider priority explicitly "
		"dictated by the ruleset. If the ruleset does not contain "
		"a <spiderPriority> xml tag, then Gigablast "
		"will spider that URL with a priority of its linking parent "
		"minus 1, "
		"resulting in breadth first spidering. A URL of spider "
		"priority X will be placed in spider priority queue #X. "
		"Many spider parameters can be configured on a per "
		"spider priority queue basis. For instance, spidering "
		"can be toggled on a per queue basis, as can link "
		"harvesting.<br><br>"
		
		"The <b>ruleset</b> you select corresponds to a file on "
		"disk named tagdb*.xml, where the '*' is a number. Each of "
		"these files is a set of rules in XML that dictate how to "
		"index and spider a document. "
		"You can add your own ruleset file to Gigablast's working "
		"directory and it will automatically be "
		"included in the ruleset drop down menu. Once a document "
		"has been indexed with a ruleset, then the corresponding "
		"ruleset file cannot be deleted without risk of corruption."
		"<br><br>"
		
		"You can have up to 32 regular expressions. "
		"Example: <b>^http://.*\\.uk/</b> would match all urls from "
		"the UK. See this "
		"<a href=/?redir="
		"http://www.phpbuilder.com/columns/dario19990616.php3>"
		"tutorial by example</a> for more information."

		"<br><br>"
		"Gigablast also supports the following special \"regular "
		"expressions\": "
		"link:gigablast and doc:quality<X and doc:quality>X.";
		*/
	m->m_cgi   = "fe";
	m->m_xml   = "filterExpression";
	m->m_max   = MAX_FILTERS;
	// array of safebufs i guess...
	m->m_off   = (char *)cr.m_regExs - x;
	// this is a safebuf, dynamically allocated string really
	m->m_type  = TYPE_SAFEBUF;//STRINGNONEMPTY
	// the size of each element in the array:
	m->m_size  = sizeof(SafeBuf);//MAX_REGEX_LEN+1;
	m->m_page  = PAGE_FILTERS;
	m->m_rowid = 1; // if we START a new row
	m->m_def   = "";
	m->m_flags = PF_REBUILDURLFILTERS;
	m++;

	m->m_title = "harvest links";
	m->m_cgi   = "hspl";
	m->m_xml   = "harvestLinks";
	m->m_max   = MAX_FILTERS;
	m->m_off   = (char *)cr.m_harvestLinks - x;
	m->m_type  = TYPE_CHECKBOX;
	m->m_def   = "1";
	m->m_page  = PAGE_FILTERS;
	m->m_rowid = 1;
	m->m_flags = PF_REBUILDURLFILTERS;
	m++;

	/*
	m->m_title = "spidering enabled";
	m->m_cgi   = "cspe";
	m->m_xml   = "spidersEnabled";
	m->m_max   = MAX_FILTERS;
	m->m_off   = (char *)cr.m_spidersEnabled - x;
	m->m_type  = TYPE_CHECKBOX;
	m->m_def   = "1";
	m->m_page  = PAGE_FILTERS;
	m->m_rowid = 1;
	m->m_flags = PF_REBUILDURLFILTERS;
	m++;
	*/

	m->m_title = "respider frequency (days)";
	m->m_cgi   = "fsf";
	m->m_xml   = "filterFrequency";
	m->m_max   = MAX_FILTERS;
	m->m_off   = (char *)cr.m_spiderFreqs - x;
	m->m_type  = TYPE_FLOAT;
	// why was this default 0 days?
	m->m_def   = "30.0"; // 0.0
	m->m_page  = PAGE_FILTERS;
	m->m_units = "days";
	m->m_rowid = 1;
	m->m_flags = PF_REBUILDURLFILTERS;
	m++;

	m->m_title = "max spiders";
	m->m_desc  = "Do not allow more than this many outstanding spiders "
		"for all urls in this priority."; // was "per rule"
	m->m_cgi   = "mspr";
	m->m_xml   = "maxSpidersPerRule";
	m->m_max   = MAX_FILTERS;
	m->m_off   = (char *)cr.m_maxSpidersPerRule - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "99";
	m->m_page  = PAGE_FILTERS;
	m->m_rowid = 1;
	m->m_flags = PF_REBUILDURLFILTERS;
	m++;

	m->m_title = "max spiders per ip";
	m->m_desc  = "Allow this many spiders per IP.";
	m->m_cgi   = "mspi";
	m->m_xml   = "maxSpidersPerIp";
	m->m_max   = MAX_FILTERS;
	m->m_off   = (char *)cr.m_spiderIpMaxSpiders - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "7";
	m->m_page  = PAGE_FILTERS;
	m->m_rowid = 1;
	m->m_flags = PF_REBUILDURLFILTERS;
	m++;

	m->m_title = "same ip wait (ms)";
	m->m_desc  = "Wait at least this long before downloading urls from "
		"the same IP address.";
	m->m_cgi   = "xg";
	m->m_xml   = "spiderIpWait";
	m->m_max   = MAX_FILTERS;
	//m->m_fixed = MAX_PRIORITY_QUEUES;
	m->m_off   = (char *)cr.m_spiderIpWaits - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "1000";
	m->m_page  = PAGE_FILTERS;
	m->m_units = "milliseconds";
	m->m_rowid = 1;
	m->m_flags = PF_REBUILDURLFILTERS;
	m++;

	/*
	m->m_title = "page quota";
	m->m_cgi   = "fsq";
	m->m_xml   = "filterQuota";
	m->m_max   = MAX_FILTERS;
	m->m_off   = (char *)cr.m_spiderQuotas - x;
	m->m_type  = TYPE_LONG_LONG;
	m->m_def   = "-1"; // -1 means no quota
	m->m_page  = PAGE_FILTERS;
	m->m_units = "pages";
	m->m_rowid = 1;
	m++;
	*/

	m->m_title = "spider priority";
	m->m_cgi   = "fsp";
	m->m_xml   = "filterPriority";
	m->m_max   = MAX_FILTERS;
	m->m_off   = (char *)cr.m_spiderPriorities - x;
	m->m_type  = TYPE_PRIORITY2; // includes UNDEFINED priority in dropdown
	m->m_page  = PAGE_FILTERS;
	m->m_rowid = 1;
	m->m_def   = "50";
	m->m_flags = PF_REBUILDURLFILTERS;
	m->m_addin = 1; // "insert" follows?
	m++;

	/*
	m->m_title = "diffbot api";
	m->m_cgi   = "dapi";
	m->m_xml   = "diffbotAPI";
	m->m_max   = MAX_FILTERS;
	m->m_off   = (char *)cr.m_spiderDiffbotApiUrl - x;
	// HACK: we print a dropdown for this but the value is a string
	// because the items in the drop down can change so we can't store
	// an item # here, it has to be a string, i.e. the diffbot api url.
	// john might add a new custom api to m_diffbotApiList at any time.
	// so we select the item in the drop down if it matches THIS string.
	m->m_type  = TYPE_SAFEBUF;//DIFFBOT_DROPDOWN;
	m->m_def   = "";
	m->m_page  = PAGE_FILTERS;
	m->m_size  = sizeof(SafeBuf);
	m->m_rowid = 1;
	m->m_addin = 1; // "insert" follows?
	m->m_flags = PF_REBUILDURLFILTERS | PF_DIFFBOT;
	m++;
	*/

	//m->m_title = "<a href=/overview.html#ruleset>ruleset</a>";
	//m->m_cgi   = "frs";
	//m->m_xml   = "filterRuleset";
	//m->m_max   = MAX_FILTERS;
	//m->m_off   = (char *)cr.m_rulesets - x;
	//m->m_type  = TYPE_RULESET; // long with dropdown of rulesets
	//m->m_page  = PAGE_FILTERS;
	//m->m_rowid = 1;
	//m->m_addin = 1; // "insert" follows?
	//m->m_def   = "";
	//m++;

	/*
	// default rule
	m->m_title = "<b>DEFAULT</b>";
	m->m_desc  = "Use the following values by default if no ruleset in "
		"tagdb matches the URL.";
	m->m_type  = TYPE_CONSTANT;
	m->m_page  = PAGE_FILTERS;
	m->m_rowid = 2;
	m->m_hdrs  = 0;
	m++;

	//m->m_cdesc  = "The default parameters if no reg exs above matched.";
	m->m_cgi   = "fsfd";
	m->m_xml   = "filterFrequencyDefault";
	m->m_off   = (char *)&cr.m_defaultSpiderFrequency - x;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "0.0";
	m->m_page  = PAGE_FILTERS;
	m->m_units = "days";
	m->m_rowid = 2;
	m->m_hdrs  = 0;
	m++;

	m->m_cgi   = "fsqd";
	m->m_xml   = "filterQuotaDefault";
	m->m_off   = (char *)&cr.m_defaultSpiderQuota - x;
	m->m_type  = TYPE_LONG_LONG;
	m->m_def   = "-1";
	m->m_page  = PAGE_FILTERS;
	m->m_units = "pages";
	m->m_rowid = 2;
	m->m_hdrs  = 0;
	m++;

	m->m_cgi   = "fspd";
	m->m_xml   = "filterPriorityDefault";
	m->m_off   = (char *)&cr.m_defaultSpiderPriority - x;
	m->m_type  = TYPE_PRIORITY2; // includes UNDEFINED priority in dropdown
	m->m_def   = "4";
	m->m_page  = PAGE_FILTERS;
	m->m_rowid = 2;
	m->m_hdrs  = 0;
	m++;
	*/

	/*
	m->m_cgi   = "frsd";
	m->m_xml   = "filterRulesetDefault";
	m->m_off   = (char *)&cr.m_defaultSiteFileNum - x;
	m->m_type  = TYPE_RULESET; // long with dropdown of rulesets
	m->m_def   = "0";
	m->m_page  = PAGE_FILTERS;
	m->m_rowid = 2;
	m->m_hdrs  = 0;
	m++;
	*/


	/*
	///////////////////////////////////////////
	// PRIORITY CONTROLS
	///////////////////////////////////////////

	// . show the priority in this column
	// . a monotnic sequence repeating each number twice,
	//   basically, div 2 is what "D2" means
	// . so we get 0,0,1,1,2,2,3,3, ...
	m->m_title = "priority";
	//m->m_desc  = "What priority is this spdier queue?";
	m->m_max   = MAX_PRIORITY_QUEUES;
	m->m_fixed = MAX_PRIORITY_QUEUES;
	m->m_type  = TYPE_MONOD2;
	m->m_page  = PAGE_PRIORITIES;
	m->m_rowid = 3;
	m++;

	// . show an alternating 0 and 1 in this column
	//   because it is type MONOM2, a monotonic sequence
	//   modulus 2.
	// . so we get 0,1,0,1,0,1,0,1,0,1, ...
	m->m_title = "is new";
	m->m_desc  = "Does this priority contain new (unindexed) urls?";
	m->m_max   = MAX_PRIORITY_QUEUES;
	m->m_fixed = MAX_PRIORITY_QUEUES;
	m->m_type  = TYPE_MONOM2;
	m->m_page  = PAGE_PRIORITIES;
	m->m_rowid = 3;
	m++;

	m->m_title = "spidering enabled";
	m->m_desc  = "Are spiders enabled for this priority?";
	m->m_cgi   = "xa";
	m->m_xml   = "spiderPrioritySpideringEnabled";
	m->m_max   = MAX_PRIORITY_QUEUES;
	m->m_fixed = MAX_PRIORITY_QUEUES;
	m->m_off   = (char *)cr.m_pq_spideringEnabled - x;
	m->m_type  = TYPE_CHECKBOX;
	m->m_def   = "1";
	m->m_page  = PAGE_PRIORITIES;
	m->m_rowid = 3;
	m++;

	m->m_title = "time slice weight";
	m->m_desc  = "What percentage of the time to draw urls from "
		"this priority?";
	m->m_cgi   = "xb";
	m->m_xml   = "spiderPriotiyTimeSlice";
	m->m_max   = MAX_PRIORITY_QUEUES;
	m->m_fixed = MAX_PRIORITY_QUEUES;
	m->m_off   = (char *)cr.m_pq_timeSlice - x;
	m->m_type  = TYPE_FLOAT;
	m->m_page  = PAGE_PRIORITIES;
	m->m_rowid = 3; // if we START a new row
	m->m_def   = "100.0";
	m->m_units = "%%";
	m++;

	m->m_title = "spidered";
	m->m_desc  = "How many urls we spidered so far last 5 minutes.";
	m->m_cgi   = "sps";
	m->m_xml   = "spiderPriotiySpidered";
	m->m_max   = MAX_PRIORITY_QUEUES;
	m->m_fixed = MAX_PRIORITY_QUEUES;
	m->m_off   = (char *)cr.m_pq_spidered - x;
	m->m_type  = TYPE_LONG_CONST;
	m->m_page  = PAGE_PRIORITIES;
	m->m_rowid = 3; // if we START a new row
	m->m_def   = "0";
	m->m_sync  = false;  // do not sync this parm
	m++;

	m->m_title = "spider links";
	m->m_desc  = "Harvest links from the content and add to spiderdb.";
	m->m_cgi   = "xc";
	m->m_xml   = "spiderPrioritySpiderLinks";
	m->m_max   = MAX_PRIORITY_QUEUES;
	m->m_fixed = MAX_PRIORITY_QUEUES;
	m->m_off   = (char *)cr.m_pq_spiderLinks - x;
	m->m_type  = TYPE_CHECKBOX;
	m->m_def   = "1";
	m->m_page  = PAGE_PRIORITIES;
	m->m_rowid = 3;
	m++;

	m->m_title = "spider same host outlinks only";
	m->m_desc  = "Harvest links to the same hostnames (www.xyz.com) "
		"and add to spiderdb.";
	m->m_cgi   = "xd";
	m->m_xml   = "spiderPrioritySpiderSameHostnameLinks";
	m->m_max   = MAX_PRIORITY_QUEUES;
	m->m_fixed = MAX_PRIORITY_QUEUES;
	m->m_off   = (char *)cr.m_pq_spiderSameHostnameLinks - x;
	m->m_type  = TYPE_CHECKBOX;
	m->m_def   = "0";
	m->m_page  = PAGE_PRIORITIES;
	m->m_rowid = 3;
	m++;

	m->m_title = "force links into queue";
	m->m_desc  = "If slated to be added to this queue, and link is "
		"already in a non-forced queue, force it into this queue. "
		"Keep a cache to reduce reptitious adds to this queue.";
	m->m_cgi   = "xdd";
	m->m_xml   = "spiderPriorityForceQueue";
	m->m_max   = MAX_PRIORITY_QUEUES;
	m->m_fixed = MAX_PRIORITY_QUEUES;
	m->m_off   = (char *)cr.m_pq_autoForceQueue - x;
	m->m_type  = TYPE_CHECKBOX;
	m->m_def   = "0";
	m->m_page  = PAGE_PRIORITIES;
	m->m_rowid = 3;
	m++;

	m->m_title = "max spiders per ip";
	m->m_desc  = "Do not allow more than this many simultaneous "
		"downloads per IP address.";
	m->m_cgi   = "xe";
	m->m_xml   = "spiderPriorityMaxSpidersPerIp";
	m->m_max   = MAX_PRIORITY_QUEUES;
	m->m_fixed = MAX_PRIORITY_QUEUES;
	m->m_off   = (char *)cr.m_pq_maxSpidersPerIp - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "1";
	m->m_page  = PAGE_PRIORITIES;
	m->m_rowid = 3;
	m++;

	m->m_title = "max spiders per domain";
	m->m_desc  = "Do not allow more than this many simultaneous "
		"downloads per domain.";
	m->m_cgi   = "xf";
	m->m_xml   = "spiderPriorityMaxSpidersPerDom";
	m->m_max   = MAX_PRIORITY_QUEUES;
	m->m_fixed = MAX_PRIORITY_QUEUES;
	m->m_off   = (char *)cr.m_pq_maxSpidersPerDom - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "1";
	m->m_page  = PAGE_PRIORITIES;
	m->m_rowid = 3;
	m++;

	m->m_title = "max respider wait (days)";
	m->m_desc  = "Do not wait longer than this before attempting to "
		"respider.";
	m->m_cgi   = "xr";
	m->m_xml   = "spiderPriorityMaxRespiderWait";
	m->m_max   = MAX_PRIORITY_QUEUES;
	m->m_fixed = MAX_PRIORITY_QUEUES;
	m->m_off   = (char *)cr.m_pq_maxRespiderWait - x;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "180.0";
	m->m_page  = PAGE_PRIORITIES;
	m->m_rowid = 3;
	m->m_units = "days";
	m++;

	m->m_title = "first respider wait (days)";
	m->m_desc  = "Reschedule a new url for respidering this many days "
		"from the first time it is actually spidered.";
	m->m_cgi   = "xfrw";
	m->m_xml   = "spiderPriorityFirstRespiderWait";
	m->m_max   = MAX_PRIORITY_QUEUES;
	m->m_fixed = MAX_PRIORITY_QUEUES;
	m->m_off   = (char *)cr.m_pq_firstRespiderWait - x;
	m->m_type  = TYPE_FLOAT;
	m->m_def   = "60.0";
	m->m_page  = PAGE_PRIORITIES;
	m->m_rowid = 3;
	m->m_units = "days";
	m++;


	m->m_title = "same ip wait (ms)";
	m->m_desc  = "Wait at least this long before downloading urls from "
		"the same IP address.";
	m->m_cgi   = "xg";
	m->m_xml   = "spiderPrioritySameIpWait";
	m->m_max   = MAX_PRIORITY_QUEUES;
	m->m_fixed = MAX_PRIORITY_QUEUES;
	m->m_off   = (char *)cr.m_pq_sameIpWait - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "10000";
	m->m_page  = PAGE_PRIORITIES;
	m->m_rowid = 3;
	m->m_units = "milliseconds";
	m++;

	m->m_title = "same domain wait (ms)";
	m->m_desc  = "Wait at least this long before downloading urls from "
		"the same domain.";
	m->m_cgi   = "xh";
	m->m_xml   = "spiderPrioritySameDomainWait";
	m->m_max   = MAX_PRIORITY_QUEUES;
	m->m_fixed = MAX_PRIORITY_QUEUES;
	m->m_off   = (char *)cr.m_pq_sameDomainWait - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "10000";
	m->m_page  = PAGE_PRIORITIES;
	m->m_rowid = 3;
	m->m_units = "milliseconds";
	m++;
	*/

	///////////////////////////////////////////
	// SITEDB FILTERS
	///////////////////////////////////////////

	/*
	m->m_title = "site expression";
	m->m_desc  = "The site of a url is a substring of that url, which "
		"defined a set of urls which are all primarily controlled "
		"by the same entity. The smallest such site of a url is "
		"returned, because a url can have multiple sites. Like "
		"fred.blogspot.com is a site and the blogspot.com site "
		"contains that site.";
	m->m_cgi   = "sdbfe";
	m->m_xml   = "siteExpression";
	m->m_max   = MAX_SITE_EXPRESSIONS;
	m->m_off   = (char *)cr.m_siteExpressions - x;
	m->m_type  = TYPE_STRINGNONEMPTY;
	m->m_size  = MAX_SITE_EXPRESSION_LEN+1;
	m->m_page  = PAGE_RULES;
	m->m_rowid = 1; // if we START a new row
	m->m_def   = "";
	m++;

	m->m_title = "site rule";
	m->m_cgi   = "sdbsrs";
	m->m_xml   = "siteRule";
	m->m_max   = MAX_SITE_EXPRESSIONS;
	m->m_off   = (char *)cr.m_siteRules - x;
	m->m_type  = TYPE_SITERULE;
	m->m_page  = PAGE_RULES;
	m->m_rowid = 1;
	m->m_def   = "0";
	m++;
	*/

	/*
	m->m_title = "siterec default ruleset";
	m->m_cgi   = "sdbfdr";
	m->m_xml   = "siterecDefaultRuleset";
	m->m_max   = MAX_SITEDB_FILTERS;
	m->m_off   = (char *)cr.m_sitedbFilterRulesets - x;
	m->m_type  = TYPE_RULESET;
	m->m_page  = PAGE_FILTERS2;
	m->m_rowid = 1;
	m->m_def   = "-1";
	m++;

	m->m_title = "ban subdomains";
	m->m_cgi   = "sdbbsd";
	m->m_xml   = "siterecBanSubdomains";
	m->m_max   = MAX_SITEDB_FILTERS;
	m->m_off   = (char *)cr.m_sitedbFilterBanSubdomains - x;
	m->m_type  = TYPE_BOOL;
	m->m_page  = PAGE_FILTERS2;
	m->m_rowid = 1;
	m->m_addin = 1; // "insert" follows
	m->m_def   = "0";
	m++;
	*/

// 	///////////////////////////////////////////
// 	//              SPAM CONTROLS            //
// 	///////////////////////////////////////////


// 	m->m_title = "char in url";
// 	m->m_desc  = "url has - or _ or a digit in the domain, "
// 		"has a plus in the cgi part.";
// 	m->m_cgi   = "spamctrla";
// 	m->m_off   = (char *)&cr.m_spamTests[CHAR_IN_URL] - x;
// 	m->m_type  = TYPE_LONG;
// 	m->m_page  = PAGE_SPAM;
// 	m->m_def   = "20";
// 	//m->m_smaxc = (char *)&cr.m_spamMaxes[CHAR_IN_URL] - x;
// 	m->m_group = 1;
// 	m->m_sparm = 0;
// 	m++;

// 	m->m_title = "bad tld";
// 	m->m_desc  = "tld is info or biz";
// 	m->m_cgi   = "spamctrlb";
// 	m->m_off   = (char *)&cr.m_spamTests[BAD_TLD] - x;
// 	m->m_type  = TYPE_LONG;
// 	m->m_page  = PAGE_SPAM;
// 	m->m_def   = "20";
// 	m->m_group = 0;
// 	m->m_sparm = 0;
// 	m++;

// 	m->m_title = "good tld";
// 	m->m_desc  = "tld is gov, edu or mil";
// 	m->m_cgi   = "spamctrlc";
// 	m->m_off   = (char *)&cr.m_spamTests[GOOD_TLD] - x;
// 	m->m_type  = TYPE_LONG;
// 	m->m_page  = PAGE_SPAM;
// 	m->m_def   = "-20";
// 	m->m_group = 0;
// 	m->m_sparm = 0;
// 	m++;

// 	m->m_title = "title has spammy words";
// 	m->m_desc  = "Title has spammy words, is all lower case, "
// 		"or has > 200 chars.  ";
// 	m->m_cgi   = "spamctrld";
// 	m->m_off   = (char *)&cr.m_spamTests[WORD_IN_TITLE] - x;
// 	m->m_type  = TYPE_LONG;
// 	m->m_page  = PAGE_SPAM;
// 	m->m_def   = "20";
// 	m->m_group = 0;
// 	m->m_sparm = 0;
// 	m++;

// 	m->m_title = "img src to other domains";
// 	m->m_desc  = "Page has img src to other domains.  ";
// 	m->m_cgi   = "spamctrle";
// 	m->m_off   = (char *)&cr.m_spamTests[IMG_SRC_OTHER_DOMAIN] - x;
// 	m->m_type  = TYPE_LONG;
// 	m->m_page  = PAGE_SPAM;
// 	m->m_def   = "5";
// 	m->m_group = 0;
// 	m->m_sparm = 0;
// 	m++;

// 	m->m_title = "page has spammy words";
// 	m->m_desc  = "Page has spammy words.  ";
// 	m->m_cgi   = "spamctrlf";
// 	m->m_off   = (char *)&cr.m_spamTests[SPAMMY_WORDS] - x;
// 	m->m_type  = TYPE_LONG;
// 	m->m_page  = PAGE_SPAM;
// 	m->m_def   = "5";
// 	m->m_group = 0;
// 	m->m_sparm = 0;
// 	m++;

// 	m->m_title = "consecutive link text";
// 	m->m_desc  = "Three consecutive link texts "
// 		"contain the same word.  ";
// 	m->m_cgi   = "spamctrlg";
// 	m->m_off   = (char *)&cr.m_spamTests[CONSECUTIVE_LINK_TEXT] - x;
// 	m->m_type  = TYPE_LONG;
// 	m->m_page  = PAGE_SPAM;
// 	m->m_def   = "10";
// 	m->m_group = 0;
// 	m->m_sparm = 0;
// 	m++;

// 	m->m_title = "affiliate company links";
// 	m->m_desc  = "links to amazon, allposters, or zappos.  ";
// 	m->m_cgi   = "spamctrlh";
// 	m->m_off   = (char *)&cr.m_spamTests[AFFILIATE_LINKS] - x;
// 	m->m_type  = TYPE_LONG;
// 	m->m_page  = PAGE_SPAM;
// 	m->m_def   = "10";
// 	m->m_group = 0;
// 	m->m_sparm = 0;
// 	m++;

// 	m->m_title = "affiliate in links";
// 	m->m_desc  = "Has string 'affiliate' in the links.  ";
// 	m->m_cgi   = "spamctrli";
// 	m->m_off   = (char *)&cr.m_spamTests[AFFILIATE_LINKS2] - x;
// 	m->m_type  = TYPE_LONG;
// 	m->m_page  = PAGE_SPAM;
// 	m->m_def   = "40";
// 	m->m_group = 0;
// 	m->m_sparm = 0;
// 	m++;

// 	m->m_title = "Iframe to amazon";
// 	m->m_desc  = "Has an iframe whose src is amazon.  ";
// 	m->m_cgi   = "spamctrlj";
// 	m->m_off   = (char *)&cr.m_spamTests[IFRAME_TO_AMAZON] - x;
// 	m->m_type  = TYPE_LONG;
// 	m->m_page  = PAGE_SPAM;
// 	m->m_def   = "30";
// 	m->m_group = 0;
// 	m->m_sparm = 0;
// 	m++;

// 	m->m_title = "long links";
// 	m->m_desc  = "Links to urls which are > 128 chars.  ";
// 	m->m_cgi   = "spamctrlk";
// 	m->m_off   = (char *)&cr.m_spamTests[LINKS_OVER_128_CHARS] - x;
// 	m->m_type  = TYPE_LONG;
// 	m->m_page  = PAGE_SPAM;
// 	m->m_def   = "5";
// 	m->m_group = 0;
// 	m->m_sparm = 0;
// 	m++;

// 	m->m_title = "links to queries";
// 	m->m_desc  = "links have ?q= or &q= in them.  ";
// 	m->m_cgi   = "spamctrll";
// 	m->m_off   = (char *)&cr.m_spamTests[LINKS_HAVE_QUERIES] - x;
// 	m->m_type  = TYPE_LONG;
// 	m->m_page  = PAGE_SPAM;
// 	m->m_def   = "5";
// 	m->m_group = 0;
// 	m->m_sparm = 0;
// 	m++;

// 	m->m_title = "google ad client";
// 	m->m_desc  = "Page has a google ad client.  ";
// 	m->m_cgi   = "spamctrlm";
// 	m->m_off   = (char *)&cr.m_spamTests[GOOGLE_AD_CLIENT] - x;
// 	m->m_type  = TYPE_LONG;
// 	m->m_page  = PAGE_SPAM;
// 	m->m_def   = "20";
// 	m->m_group = 0;
// 	m->m_sparm = 0;
// 	m++;

// 	m->m_title = "percent text in links";
// 	m->m_desc  = "percent of text in links (over 50 percent).  ";
// 	m->m_cgi   = "spamctrln";
// 	m->m_off   = (char *)&cr.m_spamTests[PERCENT_IN_LINKS] - x;
// 	m->m_type  = TYPE_LONG;
// 	m->m_page  = PAGE_SPAM;
// 	m->m_def   = "15";
// 	m->m_group = 0;
// 	m->m_sparm = 0;
// 	m++;

// 	m->m_title = "links to a url with a - or _ in the domain";
// 	m->m_desc  = "Links to a url with a - or _ in the domain";
// 	m->m_cgi   = "spamctrlo";
// 	m->m_off   = (char *)&cr.m_spamTests[DASH_IN_LINK] - x;
// 	m->m_type  = TYPE_LONG;
// 	m->m_page  = PAGE_SPAM;
// 	m->m_def   = "2";
// 	m->m_group = 0;
// 	m->m_sparm = 0;
// 	m++;

// 	m->m_title = "links to a url which is .info or .biz";
// 	m->m_desc  = "Links to a url which is .info or .biz.";
// 	m->m_cgi   = "spamctrlp";
// 	m->m_off   = (char *)&cr.m_spamTests[LINK_TO_BADTLD] - x;
// 	m->m_type  = TYPE_LONG;
// 	m->m_page  = PAGE_SPAM;
// 	m->m_def   = "2";
// 	m->m_group = 0;
// 	m->m_sparm = 0;
// 	m++;

// 	m->m_title = "links to a dmoz category";
// 	m->m_desc  = "Links to a dmoz category.";
// 	m->m_cgi   = "spamctrlq";
// 	m->m_off   = (char *)&cr.m_spamTests[LINKS_ARE_DMOZ_CATS] - x;
// 	m->m_type  = TYPE_LONG;
// 	m->m_page  = PAGE_SPAM;
// 	m->m_def   = "4";
// 	m->m_group = 0;
// 	m->m_sparm = 0;
// 	m++;

// 	m->m_title = "consecutive bold text";
// 	m->m_desc  = "Three consecutive bold texts "
// 		"contain the same word.  ";
// 	m->m_cgi   = "spamctrlr";
// 	m->m_off   = (char *)&cr.m_spamTests[CONSECUTIVE_BOLD_TEXT] - x;
// 	m->m_type  = TYPE_LONG;
// 	m->m_page  = PAGE_SPAM;
// 	m->m_def   = "10";
// 	m->m_group = 0;
// 	m->m_sparm = 0;
// 	m++;

// 	m->m_title = "link text doesn't match domain";
// 	m->m_desc  = "Link text looks like a domain, but the link doesn't go there";
// 	m->m_cgi   = "spamctrls";
// 	m->m_off   = (char *)&cr.m_spamTests[LINK_TEXT_NEQ_DOMAIN] - x;
// 	m->m_type  = TYPE_LONG;
// 	m->m_page  = PAGE_SPAM;
// 	m->m_def   = "10";
// 	m->m_group = 0;
// 	m->m_sparm = 0;
// 	m++;

// 	m->m_title = "force multiplier";
// 	m->m_desc  = "Multiply this by the number of spam categories "
// 		"that have points times the total points, for the final"
// 		" score.  Range between 0 and 1.";
// 	m->m_cgi   = "frcmult";
// 	m->m_off   = (char *)&cr.m_forceMultiplier - x;
// 	m->m_type  = TYPE_FLOAT;
// 	m->m_page  = PAGE_SPAM;
// 	m->m_def   = "0.01";
// 	m->m_group = 1;
// 	m->m_sparm = 0;
// 	m++;



// 	///////////////////////   MAXES FOR SPAM CONTROLS  ///////////////////////

// 	m->m_title = "max points for char in url";
// 	m->m_desc  = "Max points for url has - or _ or a digit in the domain";
// 	m->m_cgi   = "spammaxa";
// 	m->m_off   = (char *)&cr.m_spamMaxes[CHAR_IN_URL] - x;
// 	m->m_type  = TYPE_LONG;
// 	m->m_page  = PAGE_SPAM;
// 	m->m_def   = "300";
// 	m->m_group = 1;
// 	m->m_sparm = 0;
// 	m++;

// 	m->m_title = "max points for bad tld";
// 	m->m_desc  = "Max points for tld is info or biz";
// 	m->m_cgi   = "spammaxb";
// 	m->m_off   = (char *)&cr.m_spamMaxes[BAD_TLD] - x;
// 	m->m_type  = TYPE_LONG;
// 	m->m_page  = PAGE_SPAM;
// 	m->m_group = 0;
// 	m->m_def   = "300";
// 	m->m_sparm = 0;
// 	m++;

// 	m->m_title = "max points for good tld";
// 	m->m_desc  = "Max points for tld is gov, edu or mil";
// 	m->m_cgi   = "spammaxc";
// 	m->m_off   = (char *)&cr.m_spamMaxes[GOOD_TLD] - x;
// 	m->m_type  = TYPE_LONG;
// 	m->m_page  = PAGE_SPAM;
// 	m->m_def   = "300";
// 	m->m_group = 0;
// 	m->m_sparm = 0;
// 	m++;

// 	m->m_title = "max points for title has spammy words";
// 	m->m_desc  = "Max points for Title has spammy words.  ";
// 	m->m_cgi   = "spammaxd";
// 	m->m_off   = (char *)&cr.m_spamMaxes[WORD_IN_TITLE] - x;
// 	m->m_type  = TYPE_LONG;
// 	m->m_page  = PAGE_SPAM;
// 	m->m_def   = "300";
// 	m->m_group = 0;
// 	m->m_sparm = 0;
// 	m++;

// 	m->m_title = "max points for img src to other domains";
// 	m->m_desc  = "Max points for Page has img src to other domains.  ";
// 	m->m_cgi   = "spammaxe";
// 	m->m_off   = (char *)&cr.m_spamMaxes[IMG_SRC_OTHER_DOMAIN] - x;
// 	m->m_type  = TYPE_LONG;
// 	m->m_page  = PAGE_SPAM;
// 	m->m_def   = "300";
// 	m->m_group = 0;
// 	m->m_sparm = 0;
// 	m++;

// 	m->m_title = "max points for page has spammy words";
// 	m->m_desc  = "Max points for Page has spammy words.  ";
// 	m->m_cgi   = "spammaxf";
// 	m->m_off   = (char *)&cr.m_spamMaxes[SPAMMY_WORDS] - x;
// 	m->m_type  = TYPE_LONG;
// 	m->m_page  = PAGE_SPAM;
// 	m->m_def   = "300";
// 	m->m_group = 0;
// 	m->m_sparm = 0;
// 	m++;

// 	m->m_title = "max points for consecutive link text";
// 	m->m_desc  = "Max points for three consecutive link texts"
// 		"contain the same word.  ";
// 	m->m_cgi   = "spammaxg";
// 	m->m_off   = (char *)&cr.m_spamMaxes[CONSECUTIVE_LINK_TEXT] - x;
// 	m->m_type  = TYPE_LONG;
// 	m->m_page  = PAGE_SPAM;
// 	m->m_def   = "300";
// 	m->m_group = 0;
// 	m->m_sparm = 0;
// 	m++;

// 	m->m_title = "max points for affiliate company links";
// 	m->m_desc  = "Max points for links to amazon, allposters, or zappos.  ";
// 	m->m_cgi   = "spammaxh";
// 	m->m_off   = (char *)&cr.m_spamMaxes[AFFILIATE_LINKS] - x;
// 	m->m_type  = TYPE_LONG;
// 	m->m_page  = PAGE_SPAM;
// 	m->m_def   = "300";
// 	m->m_group = 0;
// 	m->m_sparm = 0;
// 	m++;

// 	m->m_title = "max points for affiliate in links";
// 	m->m_desc  = "Max points for Has string 'affiliate' in the links.  ";
// 	m->m_cgi   = "spammaxi";
// 	m->m_off   = (char *)&cr.m_spamMaxes[AFFILIATE_LINKS2] - x;
// 	m->m_type  = TYPE_LONG;
// 	m->m_page  = PAGE_SPAM;
// 	m->m_def   = "300";
// 	m->m_group = 0;
// 	m->m_sparm = 0;
// 	m++;

// 	m->m_title = "max points for Iframe to amazon";
// 	m->m_desc  = "Max points for Has an iframe whose src is amazon.  ";
// 	m->m_cgi   = "spammaxj";
// 	m->m_off   = (char *)&cr.m_spamMaxes[IFRAME_TO_AMAZON] - x;
// 	m->m_type  = TYPE_LONG;
// 	m->m_page  = PAGE_SPAM;
// 	m->m_def   = "300";
// 	m->m_group = 0;
// 	m->m_sparm = 0;
// 	m++;

// 	m->m_title = "max points for long links";
// 	m->m_desc  = "Max points for Links to urls which are > 128 chars.  ";
// 	m->m_cgi   = "spammaxk";
// 	m->m_off   = (char *)&cr.m_spamMaxes[LINKS_OVER_128_CHARS] - x;
// 	m->m_type  = TYPE_LONG;
// 	m->m_page  = PAGE_SPAM;
// 	m->m_def   = "300";
// 	m->m_group = 0;
// 	m->m_sparm = 0;
// 	m++;

// 	m->m_title = "max points for links to queries";
// 	m->m_desc  = "Max points for links have ?q= or &q= in them.  ";
// 	m->m_cgi   = "spammaxl";
// 	m->m_off   = (char *)&cr.m_spamMaxes[LINKS_HAVE_QUERIES] - x;
// 	m->m_type  = TYPE_LONG;
// 	m->m_page  = PAGE_SPAM;
// 	m->m_def   = "300";
// 	m->m_group = 0;
// 	m->m_sparm = 0;
// 	m++;

// 	m->m_title = "max points for google ad client";
// 	m->m_desc  = "Max points for Page has a google ad client.  ";
// 	m->m_cgi   = "spammaxm";
// 	m->m_off   = (char *)&cr.m_spamMaxes[GOOGLE_AD_CLIENT] - x;
// 	m->m_type  = TYPE_LONG;
// 	m->m_page  = PAGE_SPAM;
// 	m->m_def   = "300";
// 	m->m_group = 0;
// 	m->m_sparm = 0;
// 	m++;

// 	m->m_title = "max points for percent text in links";
// 	m->m_desc  = "Max points for percent of text in links (over 50 percent).  ";
// 	m->m_cgi   = "spammaxn";
// 	m->m_off   = (char *)&cr.m_spamMaxes[PERCENT_IN_LINKS] - x;
// 	m->m_type  = TYPE_LONG;
// 	m->m_page  = PAGE_SPAM;
// 	m->m_def   = "300";
// 	m->m_group = 0;
// 	m->m_sparm = 0;
// 	m++;
// 	m->m_title = "max points for links have - or _";
// 	m->m_desc  = "Max points for links have - or _";
// 	m->m_cgi   = "spammaxo";
// 	m->m_off   = (char *)&cr.m_spamMaxes[DASH_IN_LINK] - x;
// 	m->m_type  = TYPE_LONG;
// 	m->m_page  = PAGE_SPAM;
// 	m->m_def   = "300";
// 	m->m_group = 0;
// 	m->m_sparm = 0;
// 	m++;
// 	m->m_title = "max points for links to .info or .biz";
// 	m->m_desc  = "Max points for links to .info or .biz ";
// 	m->m_cgi   = "spammaxp";
// 	m->m_off   = (char *)&cr.m_spamMaxes[LINK_TO_BADTLD] - x;
// 	m->m_type  = TYPE_LONG;
// 	m->m_page  = PAGE_SPAM;
// 	m->m_def   = "300";
// 	m->m_group = 0;
// 	m->m_sparm = 0;
// 	m++;

// 	m->m_title = "max points for links to a dmoz category";
// 	m->m_desc  = "Max points for links to a dmoz category.";
// 	m->m_cgi   = "spammaxq";
// 	m->m_off   = (char *)&cr.m_spamMaxes[LINKS_ARE_DMOZ_CATS] - x;
// 	m->m_type  = TYPE_LONG;
// 	m->m_page  = PAGE_SPAM;
// 	m->m_def   = "300";
// 	m->m_group = 0;
// 	m->m_sparm = 0;
// 	m++;

// 	m->m_title = "max points for consecutive bold text";
// 	m->m_desc  = "Max points for three consecutive bold texts"
// 		"contain the same word.  ";
// 	m->m_cgi   = "spammaxr";
// 	m->m_off   = (char *)&cr.m_spamMaxes[CONSECUTIVE_BOLD_TEXT] - x;
// 	m->m_type  = TYPE_LONG;
// 	m->m_page  = PAGE_SPAM;
// 	m->m_def   = "300";
// 	m->m_group = 0;
// 	m->m_sparm = 0;
// 	m++;

// 	m->m_title = "max points for link text doesn't match domain";
// 	m->m_desc  = "Max points for link text doesn't match domain";
// 	m->m_cgi   = "spammaxs";
// 	m->m_off   = (char *)&cr.m_spamMaxes[LINK_TEXT_NEQ_DOMAIN] - x;
// 	m->m_type  = TYPE_LONG;
// 	m->m_page  = PAGE_SPAM;
// 	m->m_def   = "300";
// 	m->m_group = 0;
// 	m->m_sparm = 0;
// 	m++;




// 	///////////////////////////////////////////
// 	//          END SPAM CONTROLS            //
// 	///////////////////////////////////////////


	///////////////////////////////////////////
	//  PAGE REPAIR CONTROLS
	///////////////////////////////////////////

	m->m_title = "repair mode enabled";
	m->m_desc  = "If enabled, gigablast will repair the rdbs as "
		"specified by the parameters below. When a particular "
		"collection is in repair mode, it can not spider or merge "
		"titledb files.";
	m->m_cgi   = "rme";
	m->m_off   = (char *)&g_conf.m_repairingEnabled - g;
	m->m_type  = TYPE_BOOL;
	m->m_page  = PAGE_REPAIR;
	m->m_obj   = OBJ_CONF;
	m->m_def   = "0";
	m->m_sparm = 0;
	m->m_sync  = false;  // do not sync this parm
	m++;

	m->m_title = "collections to repair or rebuild";
	m->m_desc  = "Comma or space separated list of the collections "
		"to repair or rebuild.";
	m->m_cgi   = "rctr"; // repair collections to repair
	m->m_off   = (char *)&g_conf.m_collsToRepair - g;
	m->m_type  = TYPE_STRING;
	m->m_size  = 1024;
	m->m_def   = "";
	m->m_page  = PAGE_REPAIR;
	m->m_group = 0;
	m->m_sparm = 0;
	m++;

	m->m_title = "memory to use for repair";
	m->m_desc  = "In bytes.";
	m->m_cgi   = "rmtu"; // repair mem to use
	m->m_off   = (char *)&g_conf.m_repairMem - g;
	m->m_type  = TYPE_LONG;
	m->m_page  = PAGE_REPAIR;
	m->m_def   = "200000000";
	m->m_units = "bytes";
	m->m_group = 0;
	m->m_sparm = 0;
	m++;

	m->m_title = "max repair spiders";
	m->m_desc  = "Maximum number of outstanding inject spiders for "
		"repair.";
	m->m_cgi   = "mrps";
	m->m_off   = (char *)&g_conf.m_maxRepairSpiders - g;
	m->m_type  = TYPE_LONG;
	m->m_page  = PAGE_REPAIR;
	m->m_def   = "12";
	m->m_group = 0;
	m->m_sparm = 0;
	m++;

	m->m_title = "full rebuild";
	m->m_desc  = "If enabled, gigablast will reinject the content of "
		"all title recs into a secondary rdb system. That will "
		"the primary rdb system when complete.";
	m->m_cgi   = "rfr"; // repair full rebuild
	m->m_off   = (char *)&g_conf.m_fullRebuild - g;
	m->m_type  = TYPE_BOOL;
	m->m_page  = PAGE_REPAIR;
	m->m_def   = "1";
	m->m_group = 0;
	m->m_sparm = 0;
	m++;

	m->m_title = "keep new spiderdb recs";
	m->m_desc  = "If enabled, gigablast will keep the new spiderdb "
		"records when doing the full rebuild or the spiderdb "
		"rebuild.";
	m->m_cgi   = "rfrknsr";
	m->m_off   = (char *)&g_conf.m_fullRebuildKeepNewSpiderRecs - g;
	m->m_type  = TYPE_BOOL;
	m->m_page  = PAGE_REPAIR;
	m->m_def   = "1";
	m->m_group = 0;
	m->m_sparm = 0;
	m++;

	m->m_title = "recycle link info";
	m->m_desc  = "If enabled, gigablast will recycle the link info "
		"when rebuilding titledb.";
	m->m_cgi   = "rrli"; // repair full rebuild
	m->m_off   = (char *)&g_conf.m_rebuildRecycleLinkInfo - g;
	m->m_type  = TYPE_BOOL;
	m->m_page  = PAGE_REPAIR;
	m->m_def   = "1";
	m->m_group = 0;
	m->m_sparm = 0;
	m++;

	/*
	m->m_title = "recycle imported link info";
	m->m_desc  = "If enabled, gigablast will recycle the imported "
		"link info when rebuilding titledb.";
	m->m_cgi   = "rrlit"; // repair full rebuild
	m->m_off   = (char *)&g_conf.m_rebuildRecycleLinkInfo2 - g;
	m->m_type  = TYPE_BOOL;
	m->m_page  = PAGE_REPAIR;
	m->m_def   = "1";
	m->m_group = 0;
	m->m_sparm = 0;
	m++;
	*/

	/*
	m->m_title = "remove bad pages";
	m->m_desc  = "If enabled, gigablast just scans the titledb recs "
		"in the given collection and removes those that are "
		"banned or filtered according to the url filters table. It "
		"will also lookup in tagdb.";
	m->m_cgi   = "rbadp";
	m->m_off   = (char *)&g_conf.m_removeBadPages - g;
	m->m_type  = TYPE_BOOL;
	m->m_page  = PAGE_REPAIR;
	m->m_def   = "0";
	m->m_sparm = 0;
	m++;
	*/

	m->m_title = "rebuild titledb";
	m->m_desc  = "If enabled, gigablast will rebuild this rdb";
	m->m_cgi   = "rrt"; // repair rebuild titledb
	m->m_off   = (char *)&g_conf.m_rebuildTitledb - g;
	m->m_type  = TYPE_BOOL;
	m->m_page  = PAGE_REPAIR;
	m->m_def   = "0";
	m->m_sparm = 0;
	m++;

	/*
	m->m_title = "rebuild tfndb";
	m->m_desc  = "If enabled, gigablast will rebuild this rdb";
	m->m_cgi   = "rru"; // repair rebuild tfndb
	m->m_off   = (char *)&g_conf.m_rebuildTfndb - g;
	m->m_type  = TYPE_BOOL;
	m->m_page  = PAGE_REPAIR;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_sparm = 0;
	m++;

	m->m_title = "rebuild indexdb";
	m->m_desc  = "If enabled, gigablast will rebuild this rdb";
	m->m_cgi   = "rri";
	m->m_off   = (char *)&g_conf.m_rebuildIndexdb - g;
	m->m_type  = TYPE_BOOL;
	m->m_page  = PAGE_REPAIR;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_sparm = 0;
	m++;
	*/

	m->m_title = "rebuild posdb";
	m->m_desc  = "If enabled, gigablast will rebuild this rdb";
	m->m_cgi   = "rri";
	m->m_off   = (char *)&g_conf.m_rebuildPosdb - g;
	m->m_type  = TYPE_BOOL;
	m->m_page  = PAGE_REPAIR;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_sparm = 0;
	m++;

	/*
	m->m_title = "rebuild no splits";
	m->m_desc  = "If enabled, gigablast will just re-add the no split "
		"lists from all the current title recs back into indexdb.";
	m->m_cgi   = "rns";
	m->m_off   = (char *)&g_conf.m_rebuildNoSplits - g;
	m->m_type  = TYPE_BOOL;
	m->m_page  = PAGE_REPAIR;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_sparm = 0;
	m++;

	m->m_title = "rebuild datedb";
	m->m_desc  = "If enabled, gigablast will rebuild this rdb";
	m->m_cgi   = "rrd";
	m->m_off   = (char *)&g_conf.m_rebuildDatedb - g;
	m->m_type  = TYPE_BOOL;
	m->m_page  = PAGE_REPAIR;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_sparm = 0;
	m++;

	m->m_title = "rebuild checksumdb";
	m->m_desc  = "If enabled, gigablast will rebuild this rdb";
	m->m_cgi   = "rrch";
	m->m_off   = (char *)&g_conf.m_rebuildChecksumdb - g;
	m->m_type  = TYPE_BOOL;
	m->m_page  = PAGE_REPAIR;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_sparm = 0;
	m++;
	*/

	m->m_title = "rebuild clusterdb";
	m->m_desc  = "If enabled, gigablast will rebuild this rdb";
	m->m_cgi   = "rrcl";
	m->m_off   = (char *)&g_conf.m_rebuildClusterdb - g;
	m->m_type  = TYPE_BOOL;
	m->m_page  = PAGE_REPAIR;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_sparm = 0;
	m++;

	m->m_title = "rebuild spiderdb";
	m->m_desc  = "If enabled, gigablast will rebuild this rdb";
	m->m_cgi   = "rrsp";
	m->m_off   = (char *)&g_conf.m_rebuildSpiderdb - g;
	m->m_type  = TYPE_BOOL;
	m->m_page  = PAGE_REPAIR;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_sparm = 0;
	m++;

	/*
	m->m_title = "rebuild tagdb";
	m->m_desc  = "If enabled, gigablast will rebuild this rdb";
	m->m_cgi   = "rrsi";
	m->m_off   = (char *)&g_conf.m_rebuildSitedb - g;
	m->m_type  = TYPE_BOOL;
	m->m_page  = PAGE_REPAIR;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_sparm = 0;
	m++;
	*/

	m->m_title = "rebuild linkdb";
	m->m_desc  = "If enabled, gigablast will rebuild this rdb";
	m->m_cgi   = "rrld";
	m->m_off   = (char *)&g_conf.m_rebuildLinkdb - g;
	m->m_type  = TYPE_BOOL;
	m->m_page  = PAGE_REPAIR;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_sparm = 0;
	m++;

	/*
	m->m_title = "rebuild tagdb";
	m->m_desc  = "If enabled, gigablast will rebuild this rdb";
	m->m_cgi   = "rrtgld";
	m->m_off   = (char *)&g_conf.m_rebuildTagdb - g;
	m->m_type  = TYPE_BOOL;
	m->m_page  = PAGE_REPAIR;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_sparm = 0;
	m++;

	m->m_title = "rebuild placedb";
	m->m_desc  = "If enabled, gigablast will rebuild this rdb";
	m->m_cgi   = "rrpld";
	m->m_off   = (char *)&g_conf.m_rebuildPlacedb - g;
	m->m_type  = TYPE_BOOL;
	m->m_page  = PAGE_REPAIR;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_sparm = 0;
	m++;

	m->m_title = "rebuild timedb";
	m->m_desc  = "If enabled, gigablast will rebuild this rdb";
	m->m_cgi   = "rrtmd";
	m->m_off   = (char *)&g_conf.m_rebuildTimedb - g;
	m->m_type  = TYPE_BOOL;
	m->m_page  = PAGE_REPAIR;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_sparm = 0;
	m++;

	m->m_title = "rebuild sectiondb";
	m->m_desc  = "If enabled, gigablast will rebuild this rdb";
	m->m_cgi   = "rrsnd";
	m->m_off   = (char *)&g_conf.m_rebuildSectiondb - g;
	m->m_type  = TYPE_BOOL;
	m->m_page  = PAGE_REPAIR;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_sparm = 0;
	m++;

	m->m_title = "rebuild revdb";
	m->m_desc  = "If enabled, gigablast will rebuild this rdb";
	m->m_cgi   = "rrrvd";
	m->m_off   = (char *)&g_conf.m_rebuildRevdb - g;
	m->m_type  = TYPE_BOOL;
	m->m_page  = PAGE_REPAIR;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_sparm = 0;
	m++;
	*/

	m->m_title = "rebuild root urls";
	m->m_desc  = "If disabled, gigablast will skip root urls.";
	m->m_cgi   = "ruru";
	m->m_off   = (char *)&g_conf.m_rebuildRoots - g;
	m->m_type  = TYPE_BOOL;
	m->m_page  = PAGE_REPAIR;
	m->m_def   = "1";
	m->m_sparm = 0;
	m++;

	m->m_title = "rebuild non-root urls";
	m->m_desc  = "If disabled, gigablast will skip non-root urls.";
	m->m_cgi   = "runru";
	m->m_off   = (char *)&g_conf.m_rebuildNonRoots - g;
	m->m_type  = TYPE_BOOL;
	m->m_page  = PAGE_REPAIR;
	m->m_def   = "1";
	m->m_group = 0;
	m->m_sparm = 0;
	m++;

	m->m_title = "skip tagdb lookup";
	m->m_desc  = "When rebuilding spiderdb and scanning it for new spiderdb "
		"records, should a tagdb lookup be performed? Runs much much "
		"faster without it. Will also keep the original doc quality and "
		"spider priority in tact.";
	m->m_cgi   = "rssl";
	m->m_off   = (char *)&g_conf.m_rebuildSkipSitedbLookup - g;
	m->m_type  = TYPE_BOOL;
	m->m_page  = PAGE_REPAIR;
	m->m_def   = "0";
	m->m_group = 0;
	m->m_sparm = 0;
	m++;

	///////////////////////////////////////////
	//          END PAGE REPAIR              //
	///////////////////////////////////////////

	///////////////////////////////////////////
	//  QUALITY AGENT CONTROLS
	///////////////////////////////////////////


	/*
	m->m_title = "all agents on";
	m->m_desc  = "Enable quality agent on all hosts for this collection";
	m->m_cgi   = "aqae";
	m->m_obj   = OBJ_COLL;
	m->m_def   = "1";
	m->m_off   = (char *)&cr.m_qualityAgentEnabled - x;
	m->m_type  = TYPE_BOOL2; // no yes or no, just a link
	m->m_page  = PAGE_QAGENT;
	m++;

	m->m_title = "all agents off";
	m->m_desc  = "Disable quality agent on all hosts for this collection";
	m->m_cgi   = "aqad";
	m->m_def   = "0";
	m->m_off   = (char *)&cr.m_qualityAgentEnabled - x;
	m->m_type  = TYPE_BOOL2; // no yes or no, just a link
	m++;

	m->m_title = "quality agent enabled";
	m->m_desc  = "If enabled, the agent will find quality modifiers for "
		"all of the sites found in titledb.";
	m->m_cgi   = "qae";
	m->m_off   = (char *)&cr.m_qualityAgentEnabled - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_cast  = 0;
	m->m_page  = PAGE_QAGENT;
	m->m_sparm = 0;
	m++;

	m->m_title = "quality agent continuous loop";
	m->m_desc  = "If enabled, the agent will loop when it reaches "
		"the end of titledb. Otherwise, it will disable itself.";
	m->m_cgi   = "qale";
	m->m_off   = (char *)&cr.m_qualityAgentLoop - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_page  = PAGE_QAGENT;
	m->m_cast  = 1;
	m->m_sparm = 0;
	m++;

	m->m_title = "ban subsites";
	m->m_desc  = "If enabled, the agent will look at the paths of"
		" its titlerec sample, if the offending spam scores"
		" all come from the same subsite, we just ban that one."
		"  Good for banning hijacked forums or spammed archives.";
	m->m_cgi   = "qabs";
	m->m_off   = (char *)&cr.m_qualityAgentBanSubSites - x;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_page  = PAGE_QAGENT;
	m->m_cast  = 1;
	m->m_sparm = 0;
	m++;

	m->m_title = "start document";
	m->m_desc  = "The agent will start at this docid when scanning "
		"titledb looking for sites.";
	m->m_cgi   = "qasd";
	m->m_off   = (char *)&cr.m_qualityAgentStartDoc - x;
	m->m_type  = TYPE_LONG_LONG;
	m->m_def   = "0";
	m->m_cast  = 1;
	m->m_page  = PAGE_QAGENT;
	m->m_sparm = 0;
	m->m_sync  = false;  // do not sync this parm
	m++;

	m->m_title = "site quality refresh rate";
	m->m_desc  = "The quality agent will try to reexamine entries in "
		"tagdb which were added more than this many seconds ago";
	m->m_cgi   = "qasqrr";
	m->m_off   = (char *)&cr.m_tagdbRefreshRate - x;
	m->m_type  = TYPE_LONG;
	m->m_page  = PAGE_QAGENT;
	m->m_group = 1;
	m->m_cast  = 1;
	m->m_def   = "2592000";
	m->m_sparm = 0;
	m++;

	m->m_title = "link samples to get";
	m->m_desc  = "Lookup the qualities of this many links in tagdb.";
	m->m_cgi   = "lstg";
	m->m_off   = (char *)&cr.m_linkSamplesToGet - x;
	m->m_type  = TYPE_LONG;
	m->m_page  = PAGE_QAGENT;
	m->m_cast  = 1;
	m->m_def   = "256";
	m->m_sparm = 0;
	m++;

	m->m_title = "min pages to evaluate";
	m->m_desc  = "The quality agent will skip this site if there are"
		" less than this many pages to evaluate.";
	m->m_cgi   = "mpte";
	m->m_off   = (char *)&cr.m_minPagesToEvaluate - x;
	m->m_type  = TYPE_LONG;
	m->m_page  = PAGE_QAGENT;
	m->m_cast  = 1;
	m->m_def   = "1";
	m->m_sparm = 0;
	m++;

	m->m_title = "link bonus divisor";
	m->m_desc  = "Decrease a page's spam score if it has a high "
		"link quality.  The bonus is computed by dividing the "
		"page's link quality by this parm.  LinkInfos older "
		"than 30 days are considered stale and are not used.";
	m->m_cgi   = "lbd";
	m->m_off   = (char *)&cr.m_linkBonusDivisor - x;
	m->m_type  = TYPE_LONG;
	m->m_page  = PAGE_QAGENT;
	m->m_cast  = 1;
	m->m_def   = "20";
	m->m_sparm = 0;
	m++;

	m->m_title = "points per banned link";
	m->m_desc  = "Subtract x points per banned site that a site links to.";
	m->m_cgi   = "nppbl";
	m->m_off   = (char *)&cr.m_negPointsPerBannedLink - x;
	m->m_type  = TYPE_LONG;
	m->m_page  = PAGE_QAGENT;
	m->m_cast  = 1;
	m->m_def   = "3";
	m->m_sparm = 0;
	m++;

	m->m_title = "points per link to different sites on the same IP";
	m->m_desc  = "Subtract x points per site linked to that is on the "
		"same IP as other links.  Good for catching domain parking "
		"lots and spammers in general, but looking up the IPs "
		"slows down the agent considerably.  (set to 0 to disable.)";
	m->m_cgi   = "pfltdssi";
	m->m_off   = (char *)&cr.m_penaltyForLinksToDifferentSiteSameIp - x;
	m->m_type  = TYPE_LONG;
	m->m_page  = PAGE_QAGENT;
	m->m_cast  = 1;
	m->m_def   = "0";
	m->m_sparm = 0;
	m++;

	m->m_title = "number of sites on an ip to sample";
	m->m_desc  = "Examine this many sites on the same ip as this site";
	m->m_cgi   = "nsoits";
	m->m_off   = (char *)&cr.m_numSitesOnIpToSample - x;
	m->m_type  = TYPE_LONG;
	m->m_page  = PAGE_QAGENT;
	m->m_cast  = 1;
	m->m_def   = "100";
	m->m_sparm = 0;
	m++;

	m->m_title = "points per banned site on ip";
	m->m_desc  = "Subtract x points from a site quality for each banned "
		"site on the ip";
	m->m_cgi   = "nppbsoi";
	m->m_off   = (char *)&cr.m_negPointsPerBannedSiteOnIp - x;
	m->m_type  = TYPE_LONG;
	m->m_page  = PAGE_QAGENT;
	m->m_cast  = 1;
	m->m_def   = "2";
	m->m_sparm = 0;
	m++;

	m->m_title = "max penalty from being on a bad IP";
	m->m_desc  = "The penalty for being on a bad IP will not"
		" exceed this value.";
	m->m_cgi   = "qampfboabi";
	m->m_off   = (char *)&cr.m_maxPenaltyFromIp - x;
	m->m_type  = TYPE_LONG;
	m->m_page  = PAGE_QAGENT;
	m->m_cast  = 1;
	m->m_def   = "-30";
	m->m_sparm = 0;
	m++;


	m->m_title = "max sites per second";
	m->m_desc  = "The agent will not process more than this many"
		" sites per second.  Can be less than 1.";
	m->m_cgi   = "msps";
	m->m_off   = (char *)&cr.m_maxSitesPerSecond - x;
	m->m_type  = TYPE_FLOAT;
	m->m_page  = PAGE_QAGENT;
	m->m_cast  = 1;
	m->m_def   = "99999.0";
	m->m_sparm = 0;
	m++;

	m->m_title = "site agent banned ruleset";
	m->m_desc  = "Site agent will assign this ruleset to documents "
		" which are determined to be low quality.";
	m->m_cgi   = "";
	m->m_off   = (char *)&cr.m_qualityAgentBanRuleset - x;
	m->m_type  = TYPE_RULESET; // long with dropdown of rulesets
	m->m_page  = PAGE_QAGENT;
	m->m_cast  = 1;
	m->m_def   = "30";
	m->m_sparm = 0;
	m++;

	m->m_title = "ban quality theshold";
	m->m_desc  = "If the site has a spam score greater than this parm, it will"
		" be inserted into the above ruleset.";
	m->m_cgi   = "tttsb";
	m->m_off   = (char *)&cr.m_siteQualityBanThreshold - x;
	m->m_type  = TYPE_LONG;
	m->m_page  = PAGE_QAGENT;
	m->m_cast  = 1;
	m->m_def   = "-100";
	m->m_sparm = 0;
	m++;

	m->m_title = "theshold to trigger site reindex";
	m->m_desc  = "If the site has a quality less than this parm, it will"
		" be added to the spider queue for reindexing";
	m->m_cgi   = "tttsr";
	m->m_off   = (char *)&cr.m_siteQualityReindexThreshold - x;
	m->m_type  = TYPE_LONG;
	m->m_page  = PAGE_QAGENT;
	m->m_cast  = 1;
	m->m_def   = "-100";
	m->m_sparm = 0;
	m++;



// 	m->m_title = "";
// 	m->m_desc  = "";
// 	m->m_cgi   = "";
// 	m->m_off   = (char *)&cr.m_ - x;
// 	m->m_type  = TYPE_LONG;
// 	m->m_page  = PAGE_QAGENT;
// 	m->m_def   = "";
// 	m->m_sparm = 0;
// 	m++;

	*/

	///////////////////////////////////////////
	//  END QUALITY AGENT CONTROLS
	///////////////////////////////////////////


	///////////////////////////////////////////
	//  AD FEED CONTROLS
	///////////////////////////////////////////
	/*
	m->m_title = "num ads in paid inclusion ad feed";
	m->m_desc  = "The number of ads we would like returned from the ad"
                     " server. This applies to all paid inclusion ads below.";
	m->m_cgi   = "apin";
	m->m_off   = (char *)&cr.m_adPINumAds - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "2";
	m->m_page  = PAGE_ADFEED;
	m++;

	m->m_title = "num ads in skyscraper ad feed";
	m->m_desc  = "The number of ads we would like returned from the ad"
                     " server. This applies to all skyscraper ads below.";
	m->m_cgi   = "assn";
	m->m_off   = (char *)&cr.m_adSSNumAds - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "5";
	m->m_page  = PAGE_ADFEED;
	m++;

	m->m_title = "skyscraper ad width";
	m->m_desc  = "The width of the skyscraper ad column in pixels";
	m->m_cgi   = "awd";
	m->m_off   = (char *)&cr.m_adWidth - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "300";
	m->m_page  = PAGE_ADFEED;
	m++;

	m->m_title = "ad feed timeout";
	m->m_desc  = "The time (in milliseconds) to wait for an ad list to be "
		     "returned before timing out and displaying the results "
		     "without any ads. This applies to all ads below.";
	m->m_cgi   = "afto";
	m->m_off   = (char *)&cr.m_adFeedTimeOut - x;
	m->m_type  = TYPE_LONG;
	m->m_def   = "1000";
	m->m_page  = PAGE_ADFEED;
	m->m_group = 0;
	m++;
        
        m->m_title = "(1) paid inclusion ad enable";
	m->m_desc  = "Enable/Disable the paid inclusion ad.";
	m->m_cgi   = "apie";
        m->m_off   = (char *)&cr.m_adPIEnable - x;
	m->m_type  = TYPE_BOOL;
	m->m_page  = PAGE_ADFEED;
	m->m_def   = "1";
	m++;

        m->m_title = "(1) paid inclusion ad feed link";
	m->m_desc  = "Full link with address and parameters to retrieve an ad "
		     "feed.  To specify parameter input: %q for query, %n "
                     "for num results, %p for page number, %i for query ip, "
                     "and %% for %.";
	m->m_cgi   = "apicgi";
	m->m_off   = (char *)cr.m_adCGI[0] - x;
	m->m_type  = TYPE_STRING;
	m->m_size  = MAX_CGI_URL;
	m->m_page  = PAGE_ADFEED;
	m->m_def   = "";
	m->m_group = 0;
	m++;

	m->m_title = "(1) paid inclusion ad feed xml result tag";
	m->m_desc  = "Specify the full xml path for a result.";
	m->m_cgi   = "apirx";
	m->m_off   = (char *)cr.m_adResultXml[0] - x;
	m->m_type  = TYPE_STRING;
	m->m_size  = MAX_XML_LEN;
	m->m_page  = PAGE_ADFEED;
	m->m_def   = "";
	m->m_group = 0;
	m++;
	
        m->m_title = "(1) paid inclusion ad feed xml title tag";
	m->m_desc  = "Specify the full xml path for the results title.";
	m->m_cgi   = "apitx";
	m->m_off   = (char *)cr.m_adTitleXml[0] - x;
	m->m_type  = TYPE_STRING;
	m->m_size  = MAX_XML_LEN;
	m->m_page  = PAGE_ADFEED;
	m->m_def   = "";
	m->m_group = 0;
	m++;

        m->m_title = "(1) paid inclusion ad feed xml description tag";
	m->m_desc  = "Specify the full xml path for the results description.";
	m->m_cgi   = "apidx";
	m->m_off   = (char *)cr.m_adDescXml[0] - x;
	m->m_type  = TYPE_STRING;
	m->m_size  = MAX_XML_LEN;
	m->m_page  = PAGE_ADFEED;
	m->m_def   = "";
	m->m_group = 0;
	m++;

        m->m_title = "(1) paid inclusion ad feed xml link tag";
	m->m_desc  = "Specify the full xml path for the results link.  This "
		     "is the link that is shown as plain text, not an actual "
		     "link, below the ad description.";
	m->m_cgi   = "apilx";
	m->m_off   = (char *)cr.m_adLinkXml[0] - x;
	m->m_type  = TYPE_STRING;
	m->m_size  = MAX_XML_LEN;
	m->m_page  = PAGE_ADFEED;
	m->m_def   = "";
	m->m_group = 0;
	m++;

        m->m_title = "(1) paid inclusion ad feed xml url tag";
	m->m_desc  = "Specify the full xml path for the results url.  This is "
		     "the link associated with the title.";
	m->m_cgi   = "apiux";
	m->m_off   = (char *)cr.m_adUrlXml[0] - x;
	m->m_type  = TYPE_STRING;
	m->m_size  = MAX_XML_LEN;
	m->m_page  = PAGE_ADFEED;
	m->m_def   = "";
	m->m_group = 0;
	m++;

        m->m_title = "(1) paid inclusion backup ad feed link";
	m->m_desc  = "Full link with address and parameters to retrieve an ad "
		     "feed.  To specify parameter input: %q for query, %n "
                     "for num results, %p for page number, %i for query ip, "
                     "and %% for %.";
	m->m_cgi   = "apicgib";
	m->m_off   = (char *)cr.m_adCGI[1] - x;
	m->m_type  = TYPE_STRING;
	m->m_size  = MAX_CGI_URL;
	m->m_page  = PAGE_ADFEED;
	m->m_def   = "";
	m++;

	m->m_title = "(1) paid inclusion backup ad feed xml result tag";
	m->m_desc  = "Specify the full xml path for a result.";
	m->m_cgi   = "apirxb";
	m->m_off   = (char *)cr.m_adResultXml[1] - x;
	m->m_type  = TYPE_STRING;
	m->m_size  = MAX_XML_LEN;
	m->m_page  = PAGE_ADFEED;
	m->m_def   = "";
	m->m_group = 0;
	m++;
	
        m->m_title = "(1) paid inclusion backup ad feed xml title tag";
	m->m_desc  = "Specify the full xml path for the results title.";
	m->m_cgi   = "apitxb";
	m->m_off   = (char *)cr.m_adTitleXml[1] - x;
	m->m_type  = TYPE_STRING;
	m->m_size  = MAX_XML_LEN;
	m->m_page  = PAGE_ADFEED;
	m->m_def   = "";
	m->m_group = 0;
	m++;

        m->m_title = "(1) paid inclusion backup ad feed xml description tag";
	m->m_desc  = "Specify the full xml path for the results description.";
	m->m_cgi   = "apidxb";
	m->m_off   = (char *)cr.m_adDescXml[1] - x;
	m->m_type  = TYPE_STRING;
	m->m_size  = MAX_XML_LEN;
	m->m_page  = PAGE_ADFEED;
	m->m_def   = "";
	m->m_group = 0;
	m++;

        m->m_title = "(1) paid inclusion backup ad feed xml link tag";
	m->m_desc  = "Specify the full xml path for the results link.  This "
		     "is the link that is shown as plain text, not an actual "
		     "link, below the ad description.";
	m->m_cgi   = "apilxb";
	m->m_off   = (char *)cr.m_adLinkXml[1] - x;
	m->m_type  = TYPE_STRING;
	m->m_size  = MAX_XML_LEN;
	m->m_page  = PAGE_ADFEED;
	m->m_def   = "";
	m->m_group = 0;
	m++;

        m->m_title = "(1) paid inclusion backup ad feed xml url tag";
	m->m_desc  = "Specify the full xml path for the results url.  This is "
		     "the link associated with the title.";
	m->m_cgi   = "apiuxb";
	m->m_off   = (char *)cr.m_adUrlXml[1] - x;
	m->m_type  = TYPE_STRING;
	m->m_size  = MAX_XML_LEN;
	m->m_page  = PAGE_ADFEED;
	m->m_def   = "";
	m->m_group = 0;
	m++;

        m->m_title = "(1) paid inclusion format text";
	m->m_desc  = "Specify the formatting text from the <div tag in";
	m->m_cgi   = "apift";
	m->m_off   = (char *)cr.m_adPIFormat - x;
	m->m_plen  = (char *)&cr.m_adPIFormatLen - x; // length of string
	m->m_type  = TYPE_STRINGBOX;
	m->m_size  = MAX_HTML_LEN + 1;
	m->m_page  = PAGE_ADFEED;
	m->m_def   = "style=\"padding: 3px;"
                     "text-align: left; background-color: "
                     "lightyellow;\"><span style=\"font-size: larger; "
                     "font-weight: bold;\">Sponsored Results</span>\n"
                     "<br><br>";
	m->m_group = 0;
	m++;

        m->m_title = "(1) skyscraper ad enable";
	m->m_desc  = "Enable/Disable the skyscraper ad.";
	m->m_cgi   = "asse";
        m->m_off   = (char *)&cr.m_adSSEnable - x;
	m->m_type  = TYPE_BOOL;
	m->m_page  = PAGE_ADFEED;
	m->m_def   = "1";
	m++;

        m->m_title = "(1) skyscraper ad feed same as paid inclusion";
	m->m_desc  = "Use the same feed CGI as used above for the paid "
                     "inclusion.";
	m->m_cgi   = "asssap";
        m->m_off   = (char *)&cr.m_adSSSameasPI - x;
	m->m_type  = TYPE_BOOL;
	m->m_page  = PAGE_ADFEED;
	m->m_def   = "0";
	m->m_group = 0;
	m++;

        m->m_title = "(1) skyscraper ad feed link";
	m->m_desc  = "Full link with address and parameters to retrieve an ad "
		     "feed.  To specify parameter input: %q for query, %n "
                     "for num results, %p for page number, %i for query ip, "
                     "and %% for %.";
	m->m_cgi   = "asscgi";
	m->m_off   = (char *)cr.m_adCGI[2] - x;
	m->m_type  = TYPE_STRING;
	m->m_size  = MAX_CGI_URL;
	m->m_page  = PAGE_ADFEED;
	m->m_def   = "";
	m->m_group = 0;
	m++;

	m->m_title = "(1) skyscraper ad feed xml result tag";
	m->m_desc  = "Specify the full xml path for a result.";
	m->m_cgi   = "assrx";
	m->m_off   = (char *)cr.m_adResultXml[2] - x;
	m->m_type  = TYPE_STRING;
	m->m_size  = MAX_XML_LEN;
	m->m_page  = PAGE_ADFEED;
	m->m_def   = "";
	m->m_group = 0;
	m++;
	
        m->m_title = "(1) skyscraper ad feed xml title tag";
	m->m_desc  = "Specify the full xml path for the results title.";
	m->m_cgi   = "asstx";
	m->m_off   = (char *)cr.m_adTitleXml[2] - x;
	m->m_type  = TYPE_STRING;
	m->m_size  = MAX_XML_LEN;
	m->m_page  = PAGE_ADFEED;
	m->m_def   = "";
	m->m_group = 0;
	m++;

        m->m_title = "(1) skyscraper ad feed xml description tag";
	m->m_desc  = "Specify the full xml path for the results description.";
	m->m_cgi   = "assdx";
	m->m_off   = (char *)cr.m_adDescXml[2] - x;
	m->m_type  = TYPE_STRING;
	m->m_size  = MAX_XML_LEN;
	m->m_page  = PAGE_ADFEED;
	m->m_def   = "";
	m->m_group = 0;
	m++;

        m->m_title = "(1) skyscraper ad feed xml link tag";
	m->m_desc  = "Specify the full xml path for the results link.  This "
		     "is the link that is shown as plain text, not an actual "
		     "link, below the ad description.";
	m->m_cgi   = "asslx";
	m->m_off   = (char *)cr.m_adLinkXml[2] - x;
	m->m_type  = TYPE_STRING;
	m->m_size  = MAX_XML_LEN;
	m->m_page  = PAGE_ADFEED;
	m->m_def   = "";
	m->m_group = 0;
	m++;

        m->m_title = "(1) skyscraper ad feed xml url tag";
	m->m_desc  = "Specify the full xml path for the results url.  This is "
		     "the link associated with the title.";
	m->m_cgi   = "assux";
	m->m_off   = (char *)cr.m_adUrlXml[2] - x;
	m->m_type  = TYPE_STRING;
	m->m_size  = MAX_XML_LEN;
	m->m_page  = PAGE_ADFEED;
	m->m_def   = "";
	m->m_group = 0;
	m++;

        m->m_title = "(1) skyscraper backup ad feed same as paid inclusion";
	m->m_desc  = "Use the same feed CGI as used above for the backup paid "
                     "inclusion.";
	m->m_cgi   = "asssapb";
        m->m_off   = (char *)&cr.m_adBSSSameasBPI - x;
	m->m_type  = TYPE_BOOL;
	m->m_page  = PAGE_ADFEED;
	m->m_def   = "0";
	m->m_group = 0;
	m++;

        m->m_title = "(1) skyscraper backup ad feed link";
	m->m_desc  = "Full link with address and parameters to retrieve an ad "
		     "feed.  To specify parameter input: %q for query, %n "
                     "for num results, %p for page number, %i for query ip, "
                     "and %% for %.";
	m->m_cgi   = "asscgib";
	m->m_off   = (char *)cr.m_adCGI[3] - x;
	m->m_type  = TYPE_STRING;
	m->m_size  = MAX_CGI_URL;
	m->m_page  = PAGE_ADFEED;
	m->m_def   = "";
	m->m_group = 0;
	m++;

	m->m_title = "(1) skyscraper backup ad feed xml result tag";
	m->m_desc  = "Specify the full xml path for a result.";
	m->m_cgi   = "assrxb";
	m->m_off   = (char *)cr.m_adResultXml[3] - x;
	m->m_type  = TYPE_STRING;
	m->m_size  = MAX_XML_LEN;
	m->m_page  = PAGE_ADFEED;
	m->m_def   = "";
	m->m_group = 0;
	m++;
	
        m->m_title = "(1) skyscraper backup ad feed xml title tag";
	m->m_desc  = "Specify the full xml path for the results title.";
	m->m_cgi   = "asstxb";
	m->m_off   = (char *)cr.m_adTitleXml[3] - x;
	m->m_type  = TYPE_STRING;
	m->m_size  = MAX_XML_LEN;
	m->m_page  = PAGE_ADFEED;
	m->m_def   = "";
	m->m_group = 0;
	m++;

        m->m_title = "(1) skyscraper backup ad feed xml description tag";
	m->m_desc  = "Specify the full xml path for the results description.";
	m->m_cgi   = "assdxb";
	m->m_off   = (char *)cr.m_adDescXml[3] - x;
	m->m_type  = TYPE_STRING;
	m->m_size  = MAX_XML_LEN;
	m->m_page  = PAGE_ADFEED;
	m->m_def   = "";
	m->m_group = 0;
	m++;

        m->m_title = "(1) skyscraper backup ad feed xml link tag";
	m->m_desc  = "Specify the full xml path for the results link.  This "
		     "is the link that is shown as plain text, not an actual "
		     "link, below the ad description.";
	m->m_cgi   = "asslxb";
	m->m_off   = (char *)cr.m_adLinkXml[3] - x;
	m->m_type  = TYPE_STRING;
	m->m_size  = MAX_XML_LEN;
	m->m_page  = PAGE_ADFEED;
	m->m_def   = "";
	m->m_group = 0;
	m++;

        m->m_title = "(1) skyscraper backup ad feed xml url tag";
	m->m_desc  = "Specify the full xml path for the results url.  This is "
		     "the link associated with the title.";
	m->m_cgi   = "assuxb";
	m->m_off   = (char *)cr.m_adUrlXml[3] - x;
	m->m_type  = TYPE_STRING;
	m->m_size  = MAX_XML_LEN;
	m->m_page  = PAGE_ADFEED;
	m->m_def   = "";
	m->m_group = 0;
	m++;
        
        m->m_title = "(1) skyscraper format text";
	m->m_desc  = "Specify the formatting text from the <div tag in";
	m->m_cgi   = "assft";
	m->m_off   = (char *)cr.m_adSSFormat - x;
	m->m_plen  = (char *)&cr.m_adSSFormatLen - x; // length of string
	m->m_size  = MAX_HTML_LEN + 1;
	m->m_type  = TYPE_STRINGBOX;
	m->m_page  = PAGE_ADFEED;
	m->m_def   = "style=\"height: 100%; padding: 3px;"
                     "text-align: center;background-color: "
                     "lightyellow;\"><span style=\""
                     "font-size: larger; font-weight: bold;\">"
                     "Sponsored Results</span><br><br> ";
	m->m_group = 0;
	m++;
	*/

	///////////////////////////////////////////
	//  END AD FEED CONTROLS
	///////////////////////////////////////////
        

	///////////////////////////////////////////
	//  SEARCH URL CONTROLS
	//  these are only specified in the search url when doing a search
	///////////////////////////////////////////


	/////
	//
	// OLDER SEARCH INPUTS
	//
	////

	m->m_title = "query";
	m->m_desc  = "The query to perform. See <a href=/help.html>help</a>.";
	m->m_obj   = OBJ_SI;
	m->m_page  = PAGE_NONE;
	m->m_soff  = (char *)&si.m_query - y;
	m->m_type  = TYPE_STRING;
	m->m_sparm = 1;
	m->m_scgi  = "q";
	m->m_size  = MAX_QUERY_LEN;
	//m->m_sprpg = 0; // do not store query, needs to be last so related 
	//m->m_sprpp = 0; // topics can append to it
	m->m_flags = PF_COOKIE | PF_WIDGET_PARM | PF_API;
	m++;

	m->m_title = "query2";
	m->m_desc  = "X is the query on which to score inlinkers.";
	m->m_obj   = OBJ_SI;
	m->m_page  = PAGE_NONE;
	m->m_soff  = (char *)&si.m_query2 - y;
	m->m_type  = TYPE_STRING;
	m->m_sparm = 1;
	m->m_scgi  = "q2";
	m->m_size  = MAX_QUERY_LEN;
	m->m_sprpg = 0; // do not store query, needs to be last so related 
        m->m_sprpp = 0; // topics can append to it
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	/*
	m->m_title = "collection";
	m->m_desc  = "X is the name of the collection.";
	m->m_soff  = (char *)&si.m_coll - y;
	m->m_type  = TYPE_STRING;
	m->m_sparm = 1;
	m->m_scgi  = "c";
	m->m_size  = MAX_COLL_LEN;
	m->m_flags = PF_COOKIE;
	m++;
	*/

	m->m_title = "number of results per query";
	m->m_desc  = "The number of events to return for the "
		"query.";
	// make it 25 not 50 since we only have like 26 balloons
	m->m_def   = "10";
	// paying clients should have no max necessarily
	//m->m_smaxc = (char *)&cr.m_maxSearchResultsPerQuery - x;
	m->m_soff  = (char *)&si.m_docsWanted - y;
	m->m_type  = TYPE_LONG;
	m->m_sparm = 1;
	m->m_scgi  = "n";
	m->m_flags = PF_WIDGET_PARM | PF_API;
	m->m_smin  = 0;
	m++;

	//m->m_title = "show turk forms";
	//m->m_desc  = "If enabled summaries in search results will be "
	//	"turkable input forms.";
	//m->m_def   = "0";
	//m->m_soff  = (char *)&si.m_getTurkForm - y;
	//m->m_type  = TYPE_BOOL;
	//m->m_sparm = 1;
	//m->m_scgi  = "turk";
	//m++;


	m->m_title = "first result num";
	m->m_desc  = "Start displaying at search result #X. Starts at 0.";
	m->m_def   = "0";
	// paying clients should have no max necessarily
	//this gets enforced elsewhere
	//m->m_smaxc = (char *)&cr.m_maxSearchResults - x;
	m->m_soff  = (char *)&si.m_firstResultNum - y;
	m->m_type  = TYPE_LONG;
	m->m_sparm = 1;
	m->m_scgi  = "s";
	m->m_smin  = 0;
	m->m_sprpg = 0;
	m->m_sprpp = 0;
	m->m_flags = PF_REDBOX;
	m++;

	m->m_title = "restrict search to this url";
	m->m_desc  = "X is the url.";
	m->m_sparm = 1;
	m->m_soff  = (char *)&si.m_url - y;
	m->m_type  = TYPE_STRING;
	m->m_size  = MAX_URL_LEN;
	m->m_scgi  = "url";
	m->m_sprpg = 0;
	m->m_sprpp = 0;
	m++;

	m->m_title = "restrict search to pages that link to this url";
	m->m_desc  = "X is the url which the pages must link to.";
	m->m_sparm = 1;
	m->m_soff  = (char *)&si.m_link - y;
	m->m_type  = TYPE_STRING;
	m->m_size  = MAX_URL_LEN;
	m->m_scgi  = "link";
	m->m_sprpg = 0;
	m->m_sprpp = 0;
	m++;

	m->m_title = "search for this phrase quoted";
	m->m_desc  = "X is the phrase which will be quoted.";
	m->m_sparm = 1;
	m->m_soff  = (char *)&si.m_quote1 - y;
	m->m_type  = TYPE_STRING;
	m->m_size  = 512;
	m->m_scgi  = "quote1";
	m->m_sprpg = 0;
	m->m_sprpp = 0;
	m++;

	m->m_title = "search for this second phrase quoted";
	m->m_desc  = "X is the phrase which will be quoted.";
	m->m_sparm = 1;
	m->m_soff  = (char *)&si.m_quote2 - y;
	m->m_type  = TYPE_STRING;
	m->m_size  = 512;
	m->m_scgi  = "quote2";
	m->m_sprpg = 0;
	m->m_sprpp = 0;
	m++;

	m->m_title = "restrict results to this site";
	m->m_desc  = "Returned results will have URLs from this site, X.";
	m->m_soff  = (char *)&si.m_site - y;
	m->m_type  = TYPE_STRING;
	m->m_sparm = 1;
	m->m_scgi  = "site";
	m->m_size  = 1024; // MAX_SITE_LEN;
	m->m_sprpg = 1;
	m->m_sprpp = 1;
	m++;

	/*
	m->m_title = "restrict results to these sites";
	m->m_desc  = "Returned results will have URLs from the "
		"space-separated list of sites, X. X can be up to 200 sites. "
		"A site can include sub folders. This is allows you to build "
		"a <a href=\"/cts.html\">Custom Topic Search Engine</a>.";
	m->m_soff  = (char *)&si.m_sites - y;
	m->m_type  = TYPE_STRING;
	m->m_size  = 32*1024; // MAX_SITES_LEN;
	m->m_sparm = 1;
	m->m_scgi  = "sites";
	m->m_sprpg = 1;
	m->m_sprpp = 1;
	m++;
	*/

	m->m_title = "require these query terms";
	m->m_desc  = "Returned results will have all the words in X.";
	m->m_soff  = (char *)&si.m_plus - y;
	m->m_type  = TYPE_STRING;
	m->m_sparm = 1;
	m->m_scgi  = "plus";
	m->m_size  = 500;
	m->m_sprpg = 0;
	m->m_sprpp = 0;
	m++;

	m->m_title = "avoid these query terms";
	m->m_desc  = "Returned results will NOT have any of the words in X.";
	m->m_soff  = (char *)&si.m_minus - y;
	m->m_type  = TYPE_STRING;
	m->m_sparm = 1;
	m->m_scgi  = "minus";
	m->m_size  = 500;
	m->m_sprpg = 0;
	m->m_sprpp = 0;
	m++;

	/*
	m->m_title = "format of the returned search results";
	m->m_desc  = "X is 0 to get back results in regular html, 1 to "
		"get back results in XML, 2 for JSON.";
	m->m_def   = "0";
	m->m_soff  = (char *)&si.m_formatStr - y;
	m->m_type  = TYPE_STRING;//CHAR;
	m->m_sparm = 1;
	m->m_scgi  = "format";
	m->m_smin  = 0;
	m->m_smax  = 12;
	m++;
	*/

	m->m_title = "highlight query terms in summaries.";
	m->m_desc  = "Use to disable or enable "
		"highlighting of the query terms in the summaries.";
	m->m_def   = "1";
	m->m_soff  = (char *)&si.m_doQueryHighlighting - y;
	m->m_type  = TYPE_BOOL;
	m->m_sparm = 1;
	m->m_cgi   = "qh";
	m->m_scgi  = "qh";
	m->m_smin  = 0;
	m->m_smax  = 8;
	m->m_sprpg = 1; // turn off for now
	m->m_sprpp = 1;
	m->m_flags = PF_API;
	m++;

	m->m_title = "cached page highlight query";
	m->m_desc  = "Highlight the terms in this query instead. For "
		"display of the cached page.";
	m->m_def   = "";
	m->m_soff  = (char *)&si.m_highlightQuery - y;
	m->m_type  = TYPE_STRING;
	m->m_sparm = 1;
	m->m_scgi  = "hq";
	m->m_size  = 1000;
	m->m_sprpg = 0; // no need to propagate this one
	m->m_sprpp = 0;
	m++;

	m->m_title = "highlight event date in summaries.";
	m->m_desc  = "X can be 0 or 1 to respectively disable or enable "
		"highlighting of the event date terms in the summaries.";
	m->m_def   = "0";
	m->m_soff  = (char *)&si.m_doDateHighlighting - y;
	m->m_type  = TYPE_BOOL;
	m->m_sparm = 1;
	m->m_scgi  = "dh";
	m->m_smin  = 0;
	m->m_smax  = 8;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	/*
	m->m_title = "limit search results to this ruleset";
	m->m_desc  = "limit search results to this ruleset";
	m->m_def   = "0";
	m->m_soff  = (char *)&si.m_ruleset - y;
	m->m_type  = TYPE_LONG;
	m->m_sparm = 1;
	m->m_scgi  = "ruleset";
	m->m_smin  = 0;
	m++;
	*/

	m->m_title = "Query match offsets";
	m->m_desc  = "Return a list of the offsets of each query word"
		"actually matched in the document.  1 means byte offset,"
		"and 2 means word offset.";
	m->m_def   = "0";
	m->m_soff  = (char *)&si.m_queryMatchOffsets - y;
	m->m_type  = TYPE_LONG;
	m->m_sparm = 1;
	m->m_scgi  = "qmo";
	m->m_smin  = 0;
	m->m_smax  = 2;
	m++;

	m->m_title = "boolean status";
	m->m_desc  = "X can be 0 or 1 or 2. 0 means the query is NOT boolean, "
		"1 means the query is boolean and 2 means to auto-detect.";
	m->m_sparm = 1;
	m->m_def   = "2";
	m->m_soff  = (char *)&si.m_boolFlag - y;
	m->m_type  = TYPE_LONG;
	m->m_scgi  = "bq";
	m->m_smin  = 0;
	m->m_smax  = 2;
	m++;

	m->m_title = "meta tags to display";
	m->m_desc  = "X is a space-separated string of <b>meta tag names</b>. "
		"Do not forget to url-encode the spaces to +'s or %%20's. "
		"Gigablast will extract the contents of these specified meta "
		"tags out of the pages listed in the search results and "
		"display that content after each summary. i.e. "
		"<i>&dt=description</i> will display the meta description of "
		"each search result. <i>&dt=description:32+keywords:64</i> "
		"will display the meta description and meta keywords of each "
		"search result and limit the fields to 32 and 64 characters "
		"respectively. When used in an XML feed the <i>&lt;display "
		"name=\"meta_tag_name\"&gt;meta_tag_content&lt;/&gt;</i> XML "
		"tag will be used to convey each requested meta tag's "
		"content.";
	m->m_soff  = (char *)&si.m_displayMetas - y;
	m->m_type  = TYPE_STRING;
	m->m_sparm = 1;
	m->m_scgi  = "dt";
	m->m_size  = 3000;
	m++;
	
	m->m_title = "use cache";
	m->m_desc  = "X is 0 if Gigablast should not read or write from "
		"any caches at any level.";
	m->m_def   = "-1";
	m->m_soff  = (char *)&si.m_useCache - y;
	m->m_type  = TYPE_LONG;
	m->m_sparm = 1;
	m->m_scgi  = "usecache";
	m++;

	m->m_title = "write to cache";
	m->m_desc  = "X is 0 if Gigablast should not write to "
		"any caches at any level.";
	m->m_def   = "-1";
	m->m_soff  = (char *)&si.m_wcache - y;
	m->m_type  = TYPE_LONG;
	m->m_sparm = 1;
	m->m_scgi  = "wcache";
	m++;

	/*
	// . you can have multiple topics= parms in you query url...
	// . this is used to set the TopicGroups array in SearchInput
	m->m_title = "related topic parameters";
	m->m_desc  = 
		"X=<b>NUM+MAX+SCAN+MIN+MAXW+META+DEL+IDF+DEDUP</b>\n"
		"<br><br>\n"
		"<b>NUM</b> is how many <b>related topics</b> you want "
		"returned.\n"
		"<br><br>\n"
		"<b>MAX</b> is the maximum number of topics to generate "
		"and store in cache, so if TW is increased, but still below "
		"MT, it will result in a fast cache hit.\n"
		"<br><br>\n"
		"<b>SCAN</b> is how many documents to scan for related "
		"topics. If this is 30, for example, then Gigablast will "
		"scan the first 30 search results for related topics.\n"
		"<br><br>\n"
		"<b>MIN</b> is the minimum score of returned topics. Ranges "
		"from 0%% to over 100%%. 50%% is considered pretty good. "
		"BUG: This must be at least 1 to get any topics back.\n"
		"<br><br>\n"
		"<b>MAXW</b> is the maximum number of words per topic.\n"
		"<br><br>\n"
		"<b>META</b> is the meta tag name to which Gigablast will "
		"restrict the content used to generate the topics. Do not "
		"specify thie field to restrict the content to the body of "
		"each document, that is the default.\n"
		"<br><br>\n"	
		"<b>DEL</b> is a single character delimeter which defines "
		"the topic candidates. All candidates must be separated from "
		"the other candidates with the delimeter. So &lt;meta "
		"name=test content=\" cat dog ; pig rabbit horse\"&gt; "
		"when using the ; as a delimeter would only have two topic "
		"candidates: \"cat dog\" and \"pig rabbit horse\". If no "
		"delimeter is provided, default funcationality is assumed.\n"
		"<br><br>\n"
		"<b>IDF</b> is 1, the default, if you want Gigablast to "
		"weight topic candidates by their idf, 0 otherwise."
		"<br><br>\n"
		"<b>DEDUP</b> is 1, the default, if the topics should be "
		"deduped. This involves removing topics that are substrings "
		"or superstrings of other higher-scoring topics."
		"<br><br>\n"
		"Example: topics=49+100+30+1+6+author+%%3B+0+0"
		"<br><br>\n"
		"The default values for those parameters with unspecifed "
		"defaults can be defined on the \"Search Controls\" page.  "
		"<br><br>\n"
		"XML feeds will contain the generated topics like: "
		"&lt;topic&gt;&lt;name&gt;&lt;![CDATA[some topic]]&gt;&lt;"
		"/name&gt;&lt;score&gt;13&lt;/score&gt;&lt;from&gt;"
		"metaTagName&lt;/from&gt;&lt;/topic&gt;"
		"<br><br>\n"
		"Even though somewhat nonstandard, you can specify multiple "
		"<i>&amp;topic=</i> parameters to get back multiple topic "
		"groups."
		"<br><br>\n"
		"Performance will decrease if you increase the MAX, SCAN or "
		"MAXW.";
	m->m_sparm = 1;
	m->m_type  = TYPE_STRING;
	m->m_size  = 512;
	m->m_scgi  = "topics";
	m->m_size  = 100;
	// MDW: NO NO NO... was causing a write breach!!! -- take this all out
	m->m_soff  = -2; // bogus offset
	//m->m_soff  = (char *)&si.m_topics - y;
	m++;
	*/

	m->m_title = "return number of docs per topic";
	m->m_desc  = "X is 1 if you want Gigablast to return the number of "
		"documents in the search results that contained each topic.";
	m->m_def   = "1";
	m->m_sparm = 1;
	m->m_soff  = (char *)&si.m_returnDocIdCount - y;
	m->m_type  = TYPE_BOOL;
	m->m_scgi  = "rdc";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "return docids per topic";
	m->m_desc  = "X is 1 if you want Gigablast to return the list of "
		"docIds from the search results that contained each topic.";
	m->m_def   = "0";
	m->m_soff  = (char *)&si.m_returnDocIds - y;
	m->m_type  = TYPE_BOOL;
	m->m_sparm = 1;
	m->m_scgi  = "rd";
	m++;

	m->m_title = "return popularity per topic";
	m->m_desc  = "X is 1 if you want Gigablast to return the popularity "
		"of each topic.";
	m->m_def   = "0";
	m->m_soff  = (char *)&si.m_returnPops - y;
	m->m_type  = TYPE_BOOL;
	m->m_sparm = 1;
	m->m_scgi  = "rp";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "niceness";
	m->m_desc  = "X can be 0 or 1. 0 is usually a faster, high-priority "
		"query, 1 is a slower, lower-priority query.";
	m->m_sparm = 1;
	m->m_def   = "0";
	m->m_soff  = (char *)&si.m_niceness - y;
	m->m_type  = TYPE_LONG;
	m->m_scgi  = "niceness";
	m->m_smin  = 0;
	m->m_smax  = 1;
	m++;

	//m->m_title = "compound list max size";
	//m->m_desc  = "X is the max size in bytes of the compound termlist. "
	//	"Each document id is 6 bytes.";
	//m->m_sparm = 1;
	//m->m_def   = "-1";
	//m->m_soff  = (char *)&si.m_compoundListMaxSize - y;
	//m->m_type  = TYPE_LONG;
	//m->m_scgi  = "clms";
	//m->m_smin  = 0;
	//m->m_priv  = 1;
	//m++;


	m->m_title = "debug flag";
	m->m_desc  = "X is 1 to log debug information, 0 otherwise.";
	m->m_sparm = 1;
	m->m_def   = "0";
	m->m_soff  = (char *)&si.m_debug - y;
	m->m_type  = TYPE_BOOL;
	m->m_scgi  = "debug";
	//m->m_priv  = 1;
	m++;

	m->m_title = "return docids only";
	m->m_desc  = "X is 1 to return only docids as query results.";
	m->m_sparm = 1;
	m->m_def   = "0";
	m->m_soff  = (char *)&si.m_docIdsOnly - y;
	m->m_type  = TYPE_BOOL;
	m->m_scgi  = "dio";
	m++;

	m->m_title = "image url";
	m->m_desc  = "X is the url of an image to co-brand on the search "
		"results page.";
	m->m_sparm = 1;
	m->m_soff  = (char *)&si.m_imgUrl - y;
	m->m_type  = TYPE_STRING;
	m->m_size  = 512;
	m->m_scgi  = "iu";
	m++;

	m->m_title = "image link";
	m->m_desc  = "X is the hyperlink to use on the image to co-brand on "
		"the search results page.";
	m->m_sparm = 1;
	m->m_soff  = (char *)&si.m_imgLink - y;
	m->m_type  = TYPE_STRING;
	m->m_size  = 512;
	m->m_scgi  = "ix";
	m++;

	m->m_title = "image width";
	m->m_desc  = "X is the width of the image on the search results page.";
	m->m_sparm = 1;
	m->m_soff  = (char *)&si.m_imgWidth - y;
	m->m_type  = TYPE_LONG;
	m->m_scgi  = "iw";
	m++;

	m->m_title = "image height";
	m->m_desc  = "X is the height of the image on the search results "
		"page.";
	m->m_sparm = 1;
	m->m_soff  = (char *)&si.m_imgHeight - y;
	m->m_type  = TYPE_LONG;
	m->m_scgi  = "ih";
	m++;

	m->m_title = "password";
	m->m_desc  = "X is the password.";
	m->m_sparm = 1;
	m->m_soff  = (char *)&si.m_pwd - y;
	m->m_type  = TYPE_STRING;
	m->m_scgi  = "pwd";
	m->m_size  = 32;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "admin override";
	m->m_desc  = "admin override";
	m->m_sparm = 1;
	m->m_soff  = (char *)&si.m_isAdmin - y;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_scgi  = "admin";
	m->m_sprpg = 1; // propagate on GET request
        m->m_sprpp = 1; // propagate on POST request
	m++;

	/*
	m->m_title = "language";
	m->m_desc  = "Language code to restrict search. 0 = All. Uses "
		"Clusterdb to filter languages. This is being phased out "
		"please do not use much, use gblang instead.";
	m->m_sparm = 1;
	m->m_soff  = (char *)&si.m_languageCode - y;
	m->m_type  = TYPE_STRING;
	m->m_size  = 5+1;
	m->m_def   = "none";
	// our google gadget gets &lang=en passed to it from google, so
	// change this!!
	m->m_scgi  = "clang";
	m++;
	*/

	m->m_title = "GB language";
	m->m_desc  = "Language code to restrict search. 0 = All. Uses "
		"the gblang: keyword to filter languages.";
	m->m_sparm = 1;
	m->m_soff  = (char *)&si.m_gblang - y;
	m->m_type  = TYPE_LONG;
	m->m_def   = "0";
	m->m_scgi  = "gblang";
	m++;

	// prepend to query
	/*
	m->m_title = "prepend";
	m->m_desc  = "prepend this to the supplied query";
	m->m_sparm = 1;
	m->m_soff  = (char *)&si.m_queryPrepend - y;
	m->m_type  = TYPE_STRING;
	m->m_size  = 40;
	m->m_def   = NULL;
	m->m_scgi  = "prepend";
	m++;
	*/

	m->m_title = "GB Country";
	m->m_desc  = "Country code to restrict search";
	m->m_sparm = 1;
	m->m_soff  = (char *)&si.m_gbcountry - y;
	m->m_type  = TYPE_STRING;
	m->m_size  = 4+1;
	m->m_def   = NULL;
	//m->m_def   = "iso-8859-1";
	m->m_scgi  = "gbcountry";
	m++;

	/*
	m->m_title = "rerank ruleset";
	m->m_desc  = "Use this ruleset to rerank the search results. Will "
 		"rerank at least the first X results specified with &amp;n=X. "
		"And be sure to say &amp;recycle=0 to recompute the quality "
		"of each page in the search results.";
	m->m_sparm = 1;
	m->m_soff  = (char *)&si.m_rerankRuleset - y;
	m->m_type  = TYPE_LONG;
	m->m_def   = "-1";
	m->m_scgi  = "rerank";
	m++;

	m->m_title = "apply ruleset to roots";
	m->m_desc  = "Recompute the quality of the root urls of each "
		"search result in order to compute the quality of that "
		"search result, since it depends on its root quality. This "
		"can take a lot longer when enabled.";
	m->m_sparm = 1;
	m->m_soff  = (char *)&si.m_artr - y;
	m->m_type  = TYPE_LONG;
	m->m_def   = "0";
	m->m_scgi  = "artr";
	m++;
	*/

	m->m_title = "show banned pages";
	m->m_desc  = "show banned pages";
	m->m_sparm = 1;
	m->m_soff  = (char *)&si.m_showBanned - y;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_scgi  = "sb";
	m++;

	m->m_title = "allow punctuation in query phrases";
	m->m_desc  = "allow punctuation in query phrases";
	m->m_sparm = 1;
	m->m_soff  = (char *)&si.m_allowPunctInPhrase - y;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "1";
	m->m_scgi  = "apip";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "use ad feed num";
	m->m_desc  = "use ad feed num";
	m->m_sparm = 1;
	m->m_soff  = (char *)&si.m_useAdFeedNum - y;
	m->m_type  = TYPE_LONG;
	m->m_def   = "0";
	m->m_scgi  = "uafn";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "do bot detection";
	m->m_desc  = "Passed in for raw feeds that want bot detection cgi "
		     "parameters passed back in the XML.";
	m->m_sparm = 1;
	m->m_soff  = (char *)&si.m_doBotDetection - y;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_scgi  = "bd";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "bot detection query";
	m->m_desc  = "Passed in for raw feeds that want bot detection cgi "
		     "parameters passed back in the XML. Use this variable "
		     "when an actual query against gigablast is not needed "
                     "(i.e. - image/video/news searches).";
	m->m_sparm = 1;
	m->m_soff  = (char *)&si.m_botDetectionQuery - y;
	m->m_type  = TYPE_STRING;
	m->m_scgi  = "bdq";
        m->m_def   = "";
	m->m_size  = MAX_QUERY_LEN;
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "queryCharset";
	m->m_desc  = "Charset in which the query is encoded";
	m->m_sparm = 1;
	m->m_soff  = (char *)&si.m_queryCharset - y;
	m->m_type  = TYPE_STRING;
	m->m_size  = 32+1;
	m->m_def   = "utf-8";
	//m->m_def   = "iso-8859-1";
	m->m_scgi  = "qcs";
	m++;

	// buzz
	m->m_title = "display inlinks";
	m->m_desc  = "Display all inlinks of each result.";
	m->m_sparm = 1;
	m->m_soff  = (char *)&si.m_displayInlinks - y;
	m->m_type  = TYPE_LONG;
	m->m_def   = "0";
	m->m_scgi  = "inlinks";
	m++;

	// buzz
	m->m_title = "display outlinks";
	m->m_desc  = "Display all outlinks of each result. outlinks=1 "
		"displays only external outlinks. outlinks=2 displays "
		"external and internal outlinks.";
	m->m_sparm = 1;
	m->m_soff  = (char *)&si.m_displayOutlinks - y;
	m->m_type  = TYPE_LONG;
	m->m_def   = "0";
	m->m_scgi  = "outlinks";
	m++;

	// buzz
	m->m_title = "display term frequencies";
	m->m_desc  = "Display Terms and Frequencies in results.";
	m->m_sparm = 1;
	m->m_soff  = (char *)&si.m_displayTermFreqs - y;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_scgi  = "tf";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	// buzz
	m->m_title = "spider results";
	m->m_desc  = "Results of this query will be forced into the spider "
		"queue for reindexing. Usage: spiderresults=X where X is the "
		"priority to spider the results.";
	m->m_sparm = 1;
	m->m_soff  = (char *)&si.m_spiderResults - y;
	m->m_type  = TYPE_LONG;
	m->m_def   = "-1";
	m->m_scgi  = "spiderresults";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	// buzz
	m->m_title = "spider result roots";
	m->m_desc  = "Root urls of the results of this query will be forced "
		"into the spider queue for reindexing. Usage: spiderresults=X "
		"where X is the priority to spider the results.";
	m->m_sparm = 1;
	m->m_soff  = (char *)&si.m_spiderResultRoots - y;
	m->m_type  = TYPE_LONG;
	m->m_def   = "-1";
	m->m_scgi  = "spiderresultroots";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	// buzz
	m->m_title = "just mark clusterlevels";
	m->m_desc  = "Check for deduping, but just mark the cluster levels "
		"and the doc deduped against, don't remove the result.";
	m->m_sparm = 1;
	m->m_soff  = (char *)&si.m_justMarkClusterLevels - y;
	m->m_type  = TYPE_BOOL;
	m->m_def   = "0";
	m->m_scgi  = "jmcl";
	m->m_flags = PF_HIDDEN | PF_NOSAVE;
	m++;

	m->m_title = "include cached copy of page";
	m->m_desc  = "Will cause a cached copy of content to be returned "
		"instead of summary.";
	m->m_sparm = 1;
	m->m_soff  = (char *)&si.m_includeCachedCopy - y;
	m->m_type  = TYPE_LONG;
	m->m_def   = "0";
	m->m_scgi  = "icc";
	m++;

	m->m_title = "get section voting info in json";
	m->m_desc  = "Will cause section voting info to be returned.";
	m->m_sparm = 1;
	m->m_soff  = (char *)&si.m_getSectionVotingInfo - y;
	m->m_type  = TYPE_CHAR;
	m->m_def   = "0";
	m->m_scgi  = "sectionvotes";
	m++;

	// for /get
	m->m_title = "docId";
	m->m_desc  = "X is the docid of the cached page to view.";
	m->m_soff  = (char *)&si.m_docId - y;
	m->m_type  = TYPE_LONG_LONG;
	m->m_sparm = 1;
	m->m_scmd  = "/get";
	m->m_def   = "0";
	m->m_scgi  = "d";
	m++;

	// for /get
	m->m_title = "strip";
	m->m_desc  = "X is 1 or 2 two strip various tags from the "
		"cached content.";
	m->m_sparm = 1;
	m->m_scmd  = "/get";
	m->m_def   = "0";
	m->m_type  = TYPE_LONG;
	m->m_soff  = (char *)&si.m_strip - y;
	m->m_scgi  = "strip";
	m++;

	// for /get
	m->m_title = "include header";
	m->m_desc  = "X is 1 to include the Gigablast header at the top of "
		"the cached page, 0 to exclude the header.";
	m->m_sparm = 1;
	m->m_scmd  = "/get";
	m->m_def   = "1";
	m->m_type  = TYPE_BOOL;
	m->m_scgi  = "ih";
	m->m_soff  = (char *)&si.m_includeHeader - y;
	m++;

	/*
	// for /get
	m->m_title = "query highlighting query";
	m->m_desc  = "X is 1 to highlight query terms in the cached page.";
	m->m_sparm = 1;
	m->m_scmd  = "/get";
	m->m_def   = "1";
	m->m_type  = TYPE_BOOL;
	m->m_scgi  = "qh";
	m->m_soff  = (char *)&si.m_queryHighlighting - y;
	m++;
	*/

	// for /addurl
	m->m_title = "url to add";
	m->m_desc  = "Used by add url page.";
	m->m_sparm = 1;
	m->m_scmd  = "/addurl";
	m->m_type  = TYPE_STRING;
	m->m_size  = MAX_URL_LEN;
	m->m_scgi  = "u";
	m->m_soff  = (char *)&si.m_url2 - y;
	m++;

	// Process.cpp calls Msg28::massConfig with &haspower=[0|1] to 
	// indicate power loss or coming back on from a power loss
	m->m_title = "power on status notificiation";
	m->m_desc  = "Indicates power is back on.";
	m->m_cgi   = "poweron";
	m->m_obj   = OBJ_CONF;
	m->m_type  = TYPE_CMD;
	m->m_func  = CommandPowerOnNotice;
	m->m_cast  = 0;
	m->m_page  = PAGE_NONE;
	m++;

	m->m_title = "power off status notificiation";
	m->m_desc  = "Indicates power is off.";
	m->m_cgi   = "poweroff";
	m->m_obj   = OBJ_CONF;
	m->m_type  = TYPE_CMD;
	m->m_func  = CommandPowerOffNotice;
	m->m_cast  = 0;
	m->m_page  = PAGE_NONE;
	m++;




	m_numParms = m - m_parms;

	// sanity check
	if ( m_numParms >= MAX_PARMS ) {
		log("admin: Boost MAX_PARMS.");
		exit(-1);
	}
	
	// make xml tag names and store in here
	static char s_tbuf [ 18000 ];
	char *p    = s_tbuf;
	char *pend = s_tbuf + 18000;
	long  size;
	char  t;

	// . set hashes of title
	// . used by Statsdb.cpp for identifying a parm
	for ( long i = 0 ; i < m_numParms ; i++ ) {
		if ( ! m_parms[i].m_title ) continue;
		m_parms[i].m_hash = hash32n ( m_parms[i].m_title );
	}

	// cgi hashes
	for ( long i = 0 ; i < m_numParms ; i++ ) {
		if ( ! m_parms[i].m_cgi ) continue;
		m_parms[i].m_cgiHash = hash32n ( m_parms[i].m_cgi );
	}

	// sanity check: ensure all cgi parms are different
	for ( long i = 0 ; i < m_numParms ; i++ ) {
	for ( long j = 0 ; j < m_numParms ; j++ ) {
		if ( j == i             ) continue;
		if ( m_parms[i].m_type == TYPE_BOOL2 ) continue;
		if ( m_parms[j].m_type == TYPE_BOOL2 ) continue;
		if ( m_parms[i].m_type == TYPE_CMD   ) continue;
		if ( m_parms[j].m_type == TYPE_CMD   ) continue;
		if ( ! m_parms[i].m_cgi ) continue;
		if ( ! m_parms[j].m_cgi ) continue;
		// a different m_scmd means a different cgi parm really...
		if ( m_parms[i].m_sparm && m_parms[j].m_sparm &&
		     strcmp ( m_parms[i].m_scmd, m_parms[j].m_scmd) != 0 )
			continue;
		if ( strcmp ( m_parms[i].m_cgi , m_parms[j].m_cgi ) != 0 &&
		     // ensure cgi hashes are different as well!
		     m_parms[i].m_cgiHash != m_parms[j].m_cgiHash )
			continue;
		log(LOG_LOGIC,"conf: Cgi parm for #%li \"%s\" "
		    "matches #%li \"%s\". Exiting.",
		    i,m_parms[i].m_cgi,j,m_parms[j].m_cgi);
		exit(-1);
	}
	}

	long mm = (long)sizeof(CollectionRec);
	if ( (long)sizeof(Conf)        > mm ) mm = (long)sizeof(Conf);
	if ( (long)sizeof(SearchInput) > mm ) mm = (long)sizeof(SearchInput);
	// . set size of each parm based on its type
	// . also do page and obj inheritance
	// . also do sanity checking
	for ( long i = 0 ; i < m_numParms ; i++ ) {
		// sanity check
		if ( m_parms[i].m_off   > mm ||
		     m_parms[i].m_soff  > mm ||
		     m_parms[i].m_smaxc > mm   ) {
			log(LOG_LOGIC,"conf: Bad offset in parm #%li %s.",
			    i,m_parms[i].m_title);
			exit(-1);
		}
		// do not allow numbers in cgi parms, they are used for
		// denoting array indices
		long j = 0;
		for ( ; m_parms[i].m_cgi && m_parms[i].m_cgi[j] ; j++ ) {
			if ( is_digit ( m_parms[i].m_cgi[j] ) ) {
				log(LOG_LOGIC,"conf: Parm #%li \"%s\" has "
				    "number in cgi name.", 
				    i,m_parms[i].m_title);
				exit(-1);
			}
		}
		// inherit page
		if ( i > 0 && m_parms[i].m_page == -1 ) 
			m_parms[i].m_page = m_parms[i-1].m_page;
		// inherit obj
		if ( i > 0 && m_parms[i].m_obj == -1 ) 
			m_parms[i].m_obj = m_parms[i-1].m_obj;
		// if its a fixed size then make sure m_size is not set
		if ( m_parms[i].m_fixed > 0 ) {
			if ( m_parms[i].m_size != 0 ) {
				log(LOG_LOGIC,"conf: Parm #%li \"%s\" is "
				    "fixed but size is not 0.", 
				    i,m_parms[i].m_title);
				exit(-1);
			}
		}
		// skip if already set
		if ( m_parms[i].m_size ) goto skipSize;
		// string sizes should already be set!
		size = 0;
		t = m_parms[i].m_type;
		if ( t == -1 ) {
			log(LOG_LOGIC,"conf: Parm #%li \"%s\" has no type.",
			    i,m_parms[i].m_title);
			exit(-1);
		}
		if ( t == TYPE_CHAR           ) size = 1;
		if ( t == TYPE_CHAR2          ) size = 1;
		if ( t == TYPE_BOOL           ) size = 1;
		if ( t == TYPE_BOOL2          ) size = 1;
		if ( t == TYPE_CHECKBOX       ) size = 1;
		if ( t == TYPE_PRIORITY       ) size = 1;
		if ( t == TYPE_PRIORITY2      ) size = 1;
		//if ( t ==TYPE_DIFFBOT_DROPDOWN) size = 1;
		if ( t == TYPE_PRIORITY_BOXES ) size = 1;
		if ( t == TYPE_RETRIES        ) size = 1;
		if ( t == TYPE_TIME           ) size = 6;
		if ( t == TYPE_DATE2          ) size = 4;
		if ( t == TYPE_DATE           ) size = 4;
		if ( t == TYPE_FLOAT          ) size = 4;
		if ( t == TYPE_IP             ) size = 4;
		if ( t == TYPE_RULESET        ) size = 4;
		if ( t == TYPE_LONG           ) size = 4;
		if ( t == TYPE_LONG_CONST     ) size = 4;
		if ( t == TYPE_LONG_LONG      ) size = 8;
		if ( t == TYPE_STRING         ) size = m_parms[i].m_size;
		if ( t == TYPE_STRINGBOX      ) size = m_parms[i].m_size;
		if ( t == TYPE_STRINGNONEMPTY ) size = m_parms[i].m_size;
		if ( t == TYPE_SITERULE       ) size = 4;

		// comments and commands do not control underlying variables
		if ( size == 0 && t != TYPE_COMMENT && t != TYPE_CMD &&
		     t != TYPE_SAFEBUF  &&
		     t != TYPE_CONSTANT &&
		     t != TYPE_MONOD2   &&
		     t != TYPE_MONOM2     ) {
			log(LOG_LOGIC,"conf: Size of parm #%li \"%s\" "
			    "not set.", i,m_parms[i].m_title);
			exit(-1);
		}
		m_parms[i].m_size = size;
	skipSize:
		// check offset
		if ( t == TYPE_COMMENT  ) continue;
		if ( t == TYPE_CMD      ) continue;
		if ( t == TYPE_CONSTANT ) continue;
		if ( t == TYPE_MONOD2   ) continue;
		if ( t == TYPE_MONOM2   ) continue;
		if ( t == TYPE_SAFEBUF  ) continue;
		// search parms do not need an offset
		if ( m_parms[i].m_off == -1 && m_parms[i].m_sparm == 0 ) {
			log(LOG_LOGIC,"conf: Parm #%li \"%s\" has no offset.",
			    i,m_parms[i].m_title);
			exit(-1);
		}
		if ( m_parms[i].m_off < -1 ) {
			log(LOG_LOGIC,"conf: Parm #%li \"%s\" has bad offset "
			    "of %li.", i,m_parms[i].m_title,m_parms[i].m_off);
			exit(-1);
		}
		if ( m->m_obj == OBJ_CONF && m->m_off >= (long)sizeof(Conf) ) {
			log("admin: Parm %s has bad m_off value.",m->m_title);
			char *xx = NULL; *xx = 0;
		}
		if (m->m_obj==OBJ_COLL&&m->m_off>=(long)sizeof(CollectionRec)){
			log("admin: Parm %s has bad m_off value.",m->m_title);
			char *xx = NULL; *xx = 0;
		}
		if ( m->m_soff >= 0 && m->m_soff >= (long)sizeof(SearchInput)){
			log("admin: Parm %s has bad m_off value.",m->m_title);
			char *xx = NULL; *xx = 0;
		}

		if ( m_parms[i].m_page == -1 ) {
			log(LOG_LOGIC,"conf: Parm #%li \"%s\" has no page.",
			    i,m_parms[i].m_title);
			exit(-1);
		}
		if ( m_parms[i].m_obj == -1 ) {
			log(LOG_LOGIC,"conf: Parm #%li \"%s\" has no object.",
			    i,m_parms[i].m_title);
			exit(-1);
		}
		//if ( ! m_parms[i].m_title[0] ) {
		//	log(LOG_LOGIC,"conf: Parm #%li \"%s\" has no title.",
		//	    i,m_parms[i].m_cgi);
		//	exit(-1);
		//}
		// continue if already have the xml name
		if ( m_parms[i].m_xml ) continue;
		// set xml based on title
		char *tt = m_parms[i].m_title;
		if ( p + gbstrlen(tt) >= pend ) {
			log(LOG_LOGIC,"conf: Not enough room to store xml "
			    "tag name in buffer.");
			exit(-1);
		}
		m_parms[i].m_xml = p;
		for ( long k = 0 ; tt[k] ; k++ ) {
			if ( ! is_alnum_a(tt[k]) ) continue;
			if ( k > 0 && tt[k-1]==' ') *p++ = to_upper_a(tt[k]);
			else                        *p++ = tt[k];
		}
		*p++ = '\0';
	}

	// set m_searchParms
	long n = 0;
	for ( long i = 0 ; i < m_numParms ; i++ ) {
		if ( ! m_parms[i].m_sparm ) continue;
		m_searchParms[n++] = &m_parms[i];
		// sanity check
		if ( m_parms[i].m_soff == -1 ) {
			log(LOG_LOGIC,"conf: SEARCH Parm #%li \"%s\" has "
			    "m_soff < 0 (offset into SearchInput).",
			    i,m_parms[i].m_title);
			exit(-1);
		}
	}
	m_numSearchParms = n;

	// . sanity check
	// . we should have it all covered!
	si.test();

	//
	// parm overlap detector
	//
	// . fill in each parm's buffer with byte #b
	// . inc b for each parm
#ifndef _VALGRIND_
	overlapTest(+1);
	overlapTest(-1);
#endif
}

void Parms::overlapTest ( char step ) {

	long start = 0;
	if ( step == -1 ) start = m_numParms - 1;

	//log("conf: Using step=%li",(long)step);

	SearchInput   tmpsi;
	CollectionRec tmpcr;
	Conf          tmpconf;
	char          b;
	char *p1 , *p2;
	long i;
	// sanity check: ensure parms do not overlap
	for ( i = start ; i < m_numParms && i >= 0 ; i += step ) {

		// skip comments
		if ( m_parms[i].m_type == TYPE_COMMENT ) continue;
		// skip if it is a broadcast switch, like "all spiders on"
		// because that modifies another parm, "spidering enabled"
		if ( m_parms[i].m_type == TYPE_BOOL2 ) continue;

		if ( m_parms[i].m_type == TYPE_SAFEBUF ) continue;

		p1 = NULL;
		if ( m_parms[i].m_obj == OBJ_COLL ) p1 = (char *)&tmpcr;
		if ( m_parms[i].m_obj == OBJ_CONF ) p1 = (char *)&tmpconf;
		if ( p1 ) p1 += m_parms[i].m_off;
		p2 = NULL;
		if ( m_parms[i].m_soff >= 0 ) 
			p2 = (char *)&tmpsi + m_parms[i].m_soff;
		long size = m_parms[i].m_size;
		// use i now
		b = (char)i;
		// string box type is a pointer!!
		if ( p1 ) memset ( p1 , b , size );
		//log("conf: setting %li bytes for %s at 0x%lx char=0x%hhx",
		//    size,m_parms[i].m_title,(long)p1,b);
		// search input uses character ptrs!!
		if ( m_parms[i].m_type == TYPE_STRINGBOX ) size = 4;
		if ( m_parms[i].m_type == TYPE_STRING    ) size = 4;
		if ( m_parms[i].m_fixed > 0 ) size *= m_parms[i].m_fixed ;
		if ( p2 ) memset ( p2 , b , size );
		//log("conf: setting %li bytes for %s at 0x%lx char=0x%hhx "
		//    "i=%li",   size,m_parms[i].m_title,(long)p2,b,i);
	}

	//
	// now make sure they are the same
	//
	if ( step == -1 ) b--;
	else              b = 0;
	char *objStr = "none";
	long  obj;
	char  infringerB;

	for ( i = 0 ; i < m_numParms ; i++ ) {

		// skip comments
		if ( m_parms[i].m_type == TYPE_COMMENT ) continue;
		// skip if it is a broadcast switch, like "all spiders on"
		// because that modifies another parm, "spidering enabled"
		if ( m_parms[i].m_type == TYPE_BOOL2 ) continue;

		if ( m_parms[i].m_type == TYPE_SAFEBUF ) continue;

		p1 = NULL;
		if ( m_parms[i].m_obj == OBJ_COLL ) p1 = (char *)&tmpcr;
		if ( m_parms[i].m_obj == OBJ_CONF ) p1 = (char *)&tmpconf;
		if ( p1 ) p1 += m_parms[i].m_off;
		p2 = NULL;
		if ( m_parms[i].m_soff >= 0 ) 
			p2 = (char *)&tmpsi + m_parms[i].m_soff;
		long size = m_parms[i].m_size;
		long j ;
		b = (char) i;
		// save it
		obj = m_parms[i].m_obj;

		//log("conf: testing %li bytes for %s at 0x%lx char=0x%hhx "
		//    "i=%li", size,m_parms[i].m_title,(long)p1,b,i);

		for ( j = 0 ; p1 && j < size ; j++ ) {
			if ( p1[j] == b ) continue;
			// this has multiple parms pointing to it!
			//if ( m_parms[i].m_type == TYPE_BOOL2 ) continue;
			// or special cases...
			//if ( p1 == (char *)&tmpconf.m_spideringEnabled ) 
			//	continue;
			// set object type
			objStr = "Conf.h";
			if ( m_parms[i].m_type == OBJ_COLL )
				objStr = "CollectionRec.h";
			// save it
			infringerB = p1[j];
			goto error;
		}
		// search input uses character ptrs!!
		if ( m_parms[i].m_type == TYPE_STRINGBOX ) size = 4;
		if ( m_parms[i].m_type == TYPE_STRING    ) size = 4;
		if ( m_parms[i].m_fixed > 0 ) size *= m_parms[i].m_fixed ;
		objStr = "SearchInput.h";

		//log("conf: testing %li bytes for %s at 0x%lx char=0x%hhx "
		//    "i=%li",  size,m_parms[i].m_title,(long)p2,b,i);

		for ( j = 0 ; p2 && j < size ; j++ ) {
			if ( p2[j] == b ) continue;
			// save it
			infringerB = p2[j];
			log("conf: got b=0x%hhx when it should have been "
			    "b=0x%hhx",p2[j],b);
			goto error;
		}
	}

	return;

 error:
	log("conf: Had a parm value collision. Parm #%li. "
	    "\"%s\" in %s has overlapped with another parm. "
	    "Your TYPE_* for this parm or a neighbor of it "
	    "does not agree with what you have declared it as in the *.h "
	    "file.",i,m_parms[i].m_title,objStr);
	if ( step == -1 ) b--;
	else              b = 0;
	// show possible parms that could have overwritten it!
	for ( i = start ; i < m_numParms && i >= 0 ; i += step ) {
		char *p1 = NULL;
		if ( m_parms[i].m_obj == OBJ_COLL ) p1 = (char *)&tmpcr;
		if ( m_parms[i].m_obj == OBJ_CONF ) p1 = (char *)&tmpconf;
		// skip if comment
		if ( m_parms[i].m_type == TYPE_COMMENT ) continue;
		// skip if no match
		//bool match = false;
		//if ( m_parms[i].m_obj == obj ) match = true;
		//if ( m_parms[i].m_sparm && 
		// NOTE: these need to be fixed!!!
		b = (char) i;
		if ( b == infringerB )
			log("conf: possible overlap with parm #%li in %s "
			    "\"%s\" "
			    "xml=%s "
			    "desc=\"%s\"",
			    i,objStr,m_parms[i].m_title,
			    m_parms[i].m_xml,
			    m_parms[i].m_desc);
	}

	log("conf: try including \"m->m_obj = OBJ_COLL;\" or "
	    "\"m->m_obj   = OBJ_CONF;\" in your parm definitions");
	log("conf: failed overlap test. exiting.");
	exit(-1);

}


bool Parm::getValueAsBool ( SearchInput *si ) {
	char *p = (char *)si + m_soff;
	return *(bool *)p;
}

long Parm::getValueAsLong ( SearchInput *si ) {
	char *p = (char *)si + m_soff;
	return *(long *)p;
}

char *Parm::getValueAsString ( SearchInput *si ) {
	char *p = (char *)si + m_soff;
	return *(char **)p;
}

/////////
//
// new functions
//
/////////

bool Parms::addNewParmToList1 ( SafeBuf *parmList ,
				collnum_t collnum ,
				char *parmValString ,
				long  occNum ,
				char *parmName ) {
	// get the parm descriptor
	Parm *m = getParmFast1 ( parmName , NULL );
	if ( ! m ) return log("parms: got bogus parm2 %s",parmName );
	return addNewParmToList2 ( parmList,collnum,parmValString,occNum,m );
}

// . make a parm rec using the prodivded string
// . used to convert http requests into a parmlist
// . string could be a float or long or long long in ascii, as well as a string
// . returns false w/ g_errno set on error
bool Parms::addNewParmToList2 ( SafeBuf *parmList ,
				collnum_t collnum , 
				char *parmValString ,
				long occNum ,
				Parm *m ) {
	// get value
	char *val = NULL;
	long valSize = 0;

	//char buf[2+MAX_COLL_LEN];

	long val32;
	long long val64;
	char val8;
	float valf;

	/*
	char *obj = NULL;
	// we might be adding a collnum if a collection that is being
	// added via the CommandAddColl0() "addColl" or "addCrawl" or
	// "addBulk" commands. they will reserve the collnum, so it might
	// not be ready yet.
	if ( collnum != -1 ) {
		CollectionRec *cr = g_collectiondb.getRec ( collnum );
		if ( cr ) obj = (char *)cr;
		//	log("parms: no coll rec for %li",(long)collnum);
		//	return false;
		//}
		//obj = (char *)cr;
	}
	else {
		obj = (char *)&g_conf;
	}
	*/


	if ( m->m_type == TYPE_STRING || 
	     m->m_type == TYPE_STRINGBOX || 
	     m->m_type == TYPE_SAFEBUF ||
	     m->m_type == TYPE_STRINGNONEMPTY ) {
		// point to string
		//val = obj + m->m_off;
		// Parm::m_size is the max string size
		//if ( occNum > 0 ) val += occNum * m->m_size;
		// stringlength + 1. no just make it the whole string in
		// case it does not use the \0 protocol
		//valSize = m->m_max;
		val = parmValString;
		// include \0
		valSize = gbstrlen(val)+1;
		// sanity
		if ( val[valSize-1] != '\0' ) { char *xx=NULL;*xx=0; }
	}
	else if ( m->m_type == TYPE_LONG ) {
		// watch out for unsigned 32-bit numbers, so use atoLL()
		val64 = atoll(parmValString);
		val = (char *)&val64;
		valSize = 4;
	}
	else if ( m->m_type == TYPE_FLOAT ) {
		valf = atof(parmValString);
		val = (char *)&valf;
		valSize = 4;
	}
	else if ( m->m_type == TYPE_LONG_LONG ) {
		val64 = atoll(parmValString);
		val = (char *)&val64;
		valSize = 8;
	}
	else if ( m->m_type == TYPE_BOOL ||
		  m->m_type == TYPE_BOOL2 ||
		  m->m_type == TYPE_CHECKBOX ||
		  m->m_type == TYPE_PRIORITY2 ||
		  m->m_type == TYPE_CHAR ) {
		val8 = atol(parmValString);
		//if ( parmValString && to_lower_a(parmValString[0]) == 'y' )
		//	val8 = 1;
		//if ( parmValString && to_lower_a(parmValString[0]) == 'n' )
		//	val8 = 0;
		val = (char *)&val8;
		valSize = 1;
	}
	// for resetting or restarting a coll i think the ascii arg is
	// the NEW reserved collnum, but for other commands then parmValString
	// will be NULL
	else if ( m->m_type == TYPE_CMD ) {
		val = parmValString;
		if ( val ) valSize = gbstrlen(val)+1;
		// scan for holes if we hit the limit
		//if ( g_collectiondb.m_numRecs >= 1LL>>sizeof(collnum_t) )
	}
	else if ( m->m_type == TYPE_IP ) {
		// point to string
		//val = obj + m->m_off;
		// Parm::m_size is the max string size
		//if ( occNum > 0 ) val += occNum * m->m_size;
		// stringlength + 1. no just make it the whole string in
		// case it does not use the \0 protocol
		val32 = atoip(parmValString);
		// store ip in binary format
		val = (char *)&val32;
		valSize = 4;
	}
	else {
		log("parms: shit unsupported parm type");
		char *xx=NULL;*xx=0;
	}

	key96_t key = makeParmKey ( collnum , m ,  occNum );

	// then key
	if ( ! parmList->safeMemcpy ( &key , sizeof(key) ) )
		return false;

	// datasize
	if ( ! parmList->pushLong ( valSize ) )
		return false;

	// and data
	if ( val && valSize && ! parmList->safeMemcpy ( val , valSize ) )
		return false;

	return true;
}

// g_parms.addCurrentParmToList1 ( &parmList , cr , "spiderRoundNum" ); 
bool Parms::addCurrentParmToList1 ( SafeBuf *parmList ,
				    CollectionRec *cr , 
				    char *parmName ) {
	collnum_t collnum = -1;
	if ( cr ) collnum = cr->m_collnum;
	// get the parm descriptor
	long occNum;
	Parm *m = getParmFast1 ( parmName , &occNum );
	if ( ! m ) return log("parms: got bogus parm1 %s",parmName );
	return addCurrentParmToList2 ( parmList , collnum, -1 , m );
}

// . use the current value of the parm to make this record
// . parm class itself already helps us reference the binary parm value
bool Parms::addCurrentParmToList2 ( SafeBuf *parmList ,
				    collnum_t collnum , 
				    long occNum ,
				    Parm *m ) {

	char *obj = NULL;

	if ( collnum != -1 ) {
		CollectionRec *cr = g_collectiondb.getRec ( collnum );
		if ( ! cr ) return false;
		obj = (char *)cr;
	}
	else {
		obj = (char *)&g_conf;
	}

	char *data = obj + m->m_off;
	// Parm::m_size is the max string size
	long dataSize = m->m_size;
	if ( occNum > 0 ) data += occNum * m->m_size;

	if ( m->m_type == TYPE_STRING || 
	     m->m_type == TYPE_STRINGBOX || 
	     m->m_type == TYPE_SAFEBUF ||
	     m->m_type == TYPE_STRINGNONEMPTY )
		// include \0 in string
		dataSize = gbstrlen(data) + 1;

	// if a safebuf, point to the string within
	if ( m->m_type == TYPE_SAFEBUF ) {
		SafeBuf *sb = (SafeBuf *)data;
		data = sb->getBufStart();
		dataSize = sb->length();
		// sanity
		if ( dataSize > 0 && !data[dataSize-1]){char *xx=NULL;*xx=0;}
		// include the \0 since we do it for strings above
		if ( dataSize > 0 ) dataSize++;
		// empty? make it \0 then to be like strings i guess
		if ( dataSize == 0 ) {
			data = "\0";
			dataSize = 1;
		}
		// sanity check
		if ( dataSize > 0 && data[dataSize-1] ) {char *xx=NULL;*xx=0;}
		// if just a \0 then make it empty
		//if ( dataSize && !data[0] ) {
		//	data = NULL;
		//	dataSize = 0;
		//}
	}

	//long occNum = -1;
	key96_t key = makeParmKey ( collnum , m ,  occNum );

	// then key
	if ( ! parmList->safeMemcpy ( &key , sizeof(key) ) )
		return false;

	// size
	if ( ! parmList->pushLong ( dataSize ) )
		return false;

	// and data
	if ( dataSize && ! parmList->safeMemcpy ( data , dataSize ) )
		return false;

	return true;
}


// returns false and sets g_errno on error
bool Parms::convertHttpRequestToParmList (HttpRequest *hr, SafeBuf *parmList,
					  long page ){

	// false = useDefaultRec?
	CollectionRec *cr = g_collectiondb.getRec ( hr , false );

	//if ( c ) {
	//	cr = g_collectiondb.getRec ( hr );
	//	if ( ! cr ) log("parms: coll not found");
	//}
	
	// might be g_conf specific, not coll specific
	//bool hasPerm = false;
	// just knowing the collection name of a custom crawl means you
	// know the token, so you have permission
	//if ( cr && cr->m_isCustomCrawl ) hasPerm = true;
	//if ( hr->isLocal() ) hasPerm = true;

	// fix jenkins "GET /v2/crawl?token=crawlbottesting" request
	char *name  = hr->getString("name");
	char *token = hr->getString("token");
	//if ( ! cr && token ) hasPerm = true;

	//if ( ! hasPerm ) {
	//	//log("parms: no permission to set parms");
	//	//g_errno = ENOPERM;
	//	//return false;
	//	// just leave the parm list empty and fail silently
	//	return true;
	//}

	// we set the parms in this collnum
	collnum_t parmCollnum = -1;
	if ( cr ) parmCollnum = cr->m_collnum;

	// turn the collnum into an ascii string for providing as args
	// when &reset=1 &restart=1 &delete=1 is given along with a
	// &c= or a &name=/&token= pair.
	char oldCollName[MAX_COLL_LEN+1];
	oldCollName[0] = '\0';
	if ( cr ) sprintf(oldCollName,"%li",(long)cr->m_collnum);


	////////
	//
	// HACK: if crawlbot user supplies a token and a name, and the
	// corresponding collection does not exist then assume it is an add
	//
	////////
	char customCrawl = 0;
	char *path = hr->getPath();
	if ( strncmp(path,"/crawlbot",9) == 0 ) customCrawl = 1;
	if ( strncmp(path,"/v2/crawl",9) == 0 ) customCrawl = 1;
	if ( strncmp(path,"/v2/bulk" ,8) == 0 ) customCrawl = 2;
	bool hasAddCrawl = hr->hasField("addCrawl");
	bool hasAddBulk  = hr->hasField("addBulk");
	bool hasAddColl  = hr->hasField("addColl");
	// sometimes they try to delete a collection that is not there so do
	// not apply this logic in that case!
	bool hasDelete   = hr->hasField("delete");
	bool hasRestart  = hr->hasField("restart");
	bool hasReset    = hr->hasField("reset");
	if ( ! cr          && 
	     token         && 
	     name          && 
	     customCrawl   &&
	     ! hasDelete   &&
	     ! hasRestart  &&
	     ! hasReset    &&
	     ! hasAddCrawl && 
	     ! hasAddBulk  && 
	     ! hasAddColl     ) {
		// reserve a new collnum for adding this crawl
		parmCollnum = g_collectiondb.reserveCollNum();
		// must be there!
		if ( parmCollnum == -1 ) {
			g_errno = EBADENGINEER;
			return false;
		}
		// log it for now
		log("parms: trying to add custom crawl (%li)",
		    (long)parmCollnum);
		// formulate name
		char newName[MAX_COLL_LEN+1];
		snprintf(newName,MAX_COLL_LEN,"%s-%s",token,name);
		char *cmdStr = "addCrawl";
		if ( customCrawl == 2 ) cmdStr = "addBulk";
		// add to parm list
		if ( ! addNewParmToList1 ( parmList ,
					   parmCollnum ,
					   newName ,
					   -1 , // occNum
					   cmdStr ) )
			return false;
	}





	// loop through cgi parms
	for ( long i = 0 ; i < hr->getNumFields() ; i++ ) {
		// get cgi parm name
		char *field = hr->getField    ( i );
		// get value of the cgi field
		char *val  = hr->getValue   (i);
		// convert field to parm
		long occNum;
		Parm *m = getParmFast1 ( field , &occNum );
		if ( ! m ) continue;

		// skip if not a command parm, like "addcoll"
		if ( m->m_type != TYPE_CMD ) continue;

		//
		// HACK
		//
		// if its a resetcoll/restartcoll/addcoll we have to
		// get the next available collnum and use that for setting
		// any additional parms. that is the coll it will act on.
		if ( strcmp(m->m_cgi,"addColl") == 0 ||
		     strcmp(m->m_cgi,"addCrawl") == 0 ||
		     strcmp(m->m_cgi,"addBulk" ) == 0 ||
		     strcmp(m->m_cgi,"reset" ) == 0 ||
		     strcmp(m->m_cgi,"restart" ) == 0 ) {
			// if we wanted to we could make the data the
			// new parmCollnum since we already store the old
			// collnum in the parm rec key
			parmCollnum = g_collectiondb.reserveCollNum();
			//
			//
			// NOTE: the old collnum is in the "val" already
			// like "&reset=462" or "&addColl=test"
			//
			//
			// sanity. if all are full! we hit our limit of
			// 32k collections. should increase collnum_t from
			// short to long...
			if ( parmCollnum == -1 ) {
				g_errno = EBADENGINEER;
				return false;
			}
		}

		// if a collection name was also provided, assume that is
		// the target of the reset/delete/restart. we still
		// need PageAddDelete.cpp to work...
		if ( cr &&
		     ( strcmp(m->m_cgi,"reset" ) == 0 ||
		       strcmp(m->m_cgi,"delete" ) == 0 ||
		       strcmp(m->m_cgi,"restart" ) == 0 ) ) 
			// the collnum to reset/restart/del
			// given as a string.
			val = oldCollName;

		// add the cmd parm
		if ( ! addNewParmToList2 ( parmList ,
					   // it might be a collection-less
					   // command like 'gb stop' which
					   // uses the "save=1" parm.
					   // this is the "new" collnum to
					   // create in the case of
					   // add/reset/restart, but in the
					   // case of delete it is -1 or old.
					   parmCollnum ,
					   // the argument to the function...
					   // in the case of delete, the 
					   // collnum to delete in ascii.
					   // in the case of add, the name
					   // of the new coll. in the case
					   // of reset/restart the OLD 
					   // collnum is ascii to delete.
					   val,
					   occNum ,
					   m ) )
			return false;
	}

	// if we are one page url filters, turn off all checkboxes!
	// html should really transmit them as =0 if they are unchecked!!
	// "fe" is a url filter expression for the first row.
	//if ( hr->hasField("fe") && page == PAGE_FILTERS && cr ) {
	//	for ( long i = 0 ; i < cr->m_numRegExs ; i++ ) {
	//		//cr->m_harvestLinks  [i] = 0;
	//		//cr->m_spidersEnabled[i] = 0;
	//		if ( ! addNewParmToList2 ( parmList ,
	//					   cr->m_collnum,
	//					   "0",
	//					   i,
	//	}
	//}


	//
	// now add the parms that are NOT commands
	//
	
	// loop through cgi parms
	for ( long i = 0 ; i < hr->getNumFields() ; i++ ) {
		// get cgi parm name
		char *field = hr->getField    ( i );
		// get value of the cgi field
		char *val  = hr->getValue   (i);

		// get the occurence # if its regex. this is the row #
		// in the url filters table, since those parms repeat names.
		// url filter expression.
		//if ( strcmp(field,"fe") == 0 ) occNum++;

		// convert field to parm
		long occNum;
		Parm *m = getParmFast1 ( field , &occNum );

		//
		// map "pause" to spidering enabled
		//
		if ( strcmp(field,"pause"     ) == 0 ||
		     strcmp(field,"pauseCrawl") == 0 ) {
			m = getParmFast1 ( "cse",  &occNum);
			if      ( val && val[0] == '0' ) val = "1";
			else if ( val && val[0] == '1' ) val = "0";
			if ( ! m ) { char *xx=NULL;*xx=0; }
		}

		if ( ! m ) continue;
		if ( m->m_type == TYPE_CMD ) continue;

		// add it to a list now
		if ( ! addNewParmToList2 ( parmList ,
					   // HACK! operate on the to-be-added
					   // collrec, if there was an addcoll
					   // reset or restart coll cmd...
					   parmCollnum ,
					   val ,
					   occNum ,
					   m ) )
			return false;
	}


	return true;
}

Parm *Parms::getParmFast2 ( long cgiHash32 ) {
	static HashTableX s_pht;
	static char s_phtBuf[25000];
	static bool s_init = false;

	if ( ! s_init ) {
		// init hashtable
		s_pht.set ( 4,4, 2048, s_phtBuf,25000, false,0,"phttab" );
		// reduce hash collisions:
		s_pht.m_useKeyMagic = true;
		// wtf?
		if ( m_numParms <= 0 ) init();
		if ( m_numParms <= 0 ) { char *xx=NULL;*xx=0; }
		// fill up hashtable
		for ( long i = 0 ; i < m_numParms ; i++ ) {
			// get it
			Parm *parm = &m_parms[i];
			// skip comments
			if ( parm->m_type == TYPE_COMMENT ) continue;
			// skip if no cgi
			if ( ! parm->m_cgi ) continue;
			// get its hash of its cgi
			long ph32 = parm->m_cgiHash;
			// sanity!
			if ( s_pht.isInTable ( &ph32 ) ) {
				// get the dup guy
				Parm *dup = *(Parm **)s_pht.getValue(&ph32);
				// same underlying parm?
				// like for "all spiders on" vs.
				// "all spiders off"?
				if ( dup->m_off == parm->m_off )
					continue;
				// otherwise bitch about it and drop core
				log("parms: dup parm h32=%li "
				    "\"%s\" vs \"%s\"",
				    ph32, dup->m_title,parm->m_title);
				char *xx=NULL;*xx=0;
			}
			// add that to hash table
			s_pht.addKey ( &ph32 , &parm );
		}
		// do not do this again
		s_init = true;
	}

	Parm **pp = (Parm **)s_pht.getValue ( &cgiHash32 );
	if ( ! pp ) return NULL;
	return *pp;
}


Parm *Parms::getParmFast1 ( char *cgi , long *occNum ) {
	// strip off the %li for things like 'fe3' for example
	// because that is the occurence # for parm arrays.
	long clen = gbstrlen(cgi);

	char *d = NULL;

	if ( clen > 1 ) {
		d = cgi + clen - 1;
		while ( is_digit(*d) ) d--;
		d++;
	}

	long h32;

	// assume not an array
	if ( occNum ) *occNum = -1;

	if ( d && *d ) {
		if ( occNum ) *occNum = atol(d);
		h32 = hash32 ( cgi , d - cgi );
	}
	else 
		h32 = hash32n ( cgi );

	Parm *m = getParmFast2 ( h32 );

	if ( ! m ) return NULL;

	// the first element does not have a number after it
	if ( m->isArray() && occNum && *occNum == -1 )
		*occNum = 0;

	return m;
}

////////////
//
// functions for distrubting/syncing parms to/with all hosts
//
////////////

class ParmNode {
public:
	SafeBuf m_parmList;
	long m_numRequests;
	long m_numReplies;
	long m_numGoodReplies;
	long m_numHostsTotal;
	class ParmNode *m_prevNode;
	class ParmNode *m_nextNode;
	long long m_parmId;
	bool m_calledCallback;
	long m_startTime;
	void *m_state;
	void (* m_callback)(void *state);
	bool m_sendToGrunts;
	bool m_sendToProxies;
	long m_hostId; // -1 means send parm update to all hosts
	// . if not -1 then [m_hostId,m_hostId2] is a range
	// . used by main.cpp cmd line cmds like 'gb stop 3-5'
	long m_hostId2; 
};

static ParmNode *s_headNode = NULL;
static ParmNode *s_tailNode = NULL;
static long long s_parmId = 0LL;

// . will send the parm update request to each host and retry forever, 
//   until dead hosts come back up
// . keeps parm update requests in order received
// . returns true and sets g_errno on error
// . returns false if blocked and will call your callback
bool Parms::broadcastParmList ( SafeBuf *parmList ,
				void    *state ,
				void   (* callback)(void *) ,
				bool sendToGrunts ,
				bool sendToProxies ,
				// this is -1 if sending to all hosts
				long hostId ,
				// this is not -1 if its range [hostId,hostId2]
				long hostId2 ) {

	// empty list?
	if ( parmList->getLength() <= 0 ) return true;

	// only us? no need for this then. we now do this...
	//if ( g_hostdb.m_numHosts <= 1 ) return true;

	// make a new parm transmit node
	ParmNode *pn = (ParmNode *)mmalloc ( sizeof(ParmNode) , "parmnode" );
	if ( ! pn ) return true;
	pn->m_parmList.constructor();

	// update the ticket #. we use this to keep things ordered too.
	// this should never be zero since it starts off at zero.
	s_parmId++;

	// set it
	pn->m_parmList.stealBuf ( parmList );
	pn->m_numRequests    = 0;
	pn->m_numReplies     = 0;
	pn->m_numGoodReplies = 0;
	pn->m_numHostsTotal  = 0;
	pn->m_prevNode       = NULL;
	pn->m_nextNode       = NULL;
	pn->m_parmId         = s_parmId; // take a ticket
	pn->m_calledCallback = false;
	pn->m_startTime      = getTimeLocal();
	pn->m_state          = state;
	pn->m_callback       = callback;
	pn->m_sendToGrunts   = sendToGrunts;
	pn->m_sendToProxies  = sendToProxies;
	pn->m_hostId         = hostId;
	pn->m_hostId2        = hostId2; // a range? then not -1 here.

	// store it ordered in our linked list of parm transmit nodes
	if ( ! s_tailNode ) {
		s_headNode = pn;
		s_tailNode = pn;
	}
	else {
		// link pn at end of tail
		s_tailNode->m_nextNode = pn;
		pn->m_prevNode = s_tailNode;
		// pn becomes the new tail
		s_tailNode = pn;
	}

	// just the regular proxies, not compression proxies
	if ( pn->m_sendToProxies ) 
		pn->m_numHostsTotal += g_hostdb.getNumProxies();

	if ( pn->m_sendToGrunts )
		pn->m_numHostsTotal += g_hostdb.getNumGrunts();

	if ( hostId >= 0 )
		pn->m_numHostsTotal = 1;

	// pump the parms out to other hosts in the network
	doParmSendingLoop ( );

	// . if waiting for more replies to come in that should be in soon
	// . doParmSendingLoop() is called when a reply comes in so that
	//   the next requests can be sent out
	//if ( waitingForLiveHostsToReply() ) return false;

	// all done. how did this happen?
	//return true;

	// wait for replies
	return false;
}

void tryToCallCallbacks ( ) {

	ParmNode *pn = s_headNode;
	long now = getTimeLocal();

	for ( ; pn ; pn = pn->m_nextNode ) {
		// skip if already called callback
		if ( pn->m_calledCallback ) continue;
		// should we call the callback?
		bool callIt = false;
		// 8 seconds is enough to wait for all replies to come in
		if ( now - pn->m_startTime > 8 ) callIt = true;
		if ( pn->m_numReplies >= pn->m_numRequests ) callIt = true;
		if ( ! callIt ) continue;
		// callback is NULL for updating parms like spiderRoundNum
		// in Spider.cpp
		if ( pn->m_callback ) pn->m_callback ( pn->m_state );
		pn->m_calledCallback = true;
	}
}

void gotParmReplyWrapper ( void *state , UdpSlot *slot ) {

	// don't let upserver free the send buf! that's the ParmNode parmlist
	slot->m_sendBufAlloc = NULL;

	// in case host table is dynamically modified, go by #
	Host *h = g_hostdb.getHost((long)state);

	long parmId = h->m_currentParmIdInProgress;

	ParmNode *pn = h->m_currentNodePtr;

	// inc this count
	pn->m_numReplies++;

	// nothing in progress now
	h->m_currentParmIdInProgress = 0;
	h->m_currentNodePtr = NULL;

	// this is usually timeout on a dead host i guess
	if ( g_errno ) {
		log("parms: got parm update reply from host #%li: %s",
		    h->m_hostId,mstrerror(g_errno));
	}


	// . note it so we do not retry every 1ms!
	// . and only retry on time outs or no mem errors for now...
	// . it'll retry once every 10 seconds using the sleep
	//   wrapper below
	if ( g_errno != EUDPTIMEDOUT &&  g_errno != ENOMEM ) 
		g_errno = 0;

	if ( g_errno ) {
		// remember error info for retry
		h->m_lastTryError = g_errno;
		h->m_lastTryTime = getTimeLocal();
		// if a host timed out he could be dead, so try to call
		// the callback for this "pn" anyway. if the only hosts we
		// do not have replies for are dead, then we'll call the
		// callback, but still keep trying to send to them.
		tryToCallCallbacks ();
		// try to send more i guess? i think this is right otherwise
		// the callback might not ever get called
		g_parms.doParmSendingLoop();
		return;
	}

	// no error, otherwise
	h->m_lastTryError = 0;

	// successfully completed
	h->m_lastParmIdCompleted = parmId;

	// inc this count
	pn->m_numGoodReplies++;

	// . this will try to call any callback that can be called
	// . for instances, if the "pn" has recvd all the replies
	// . OR if the remaining hosts are "DEAD"
	// . the callback is in the "pn"
	tryToCallCallbacks ();

	// nuke it?
	if ( pn->m_numGoodReplies >= pn->m_numHostsTotal &&
	     pn->m_numReplies >= pn->m_numRequests ) {

		// . we must always be the head lest we send out of order.
		// . ParmNodes only destined to a specific hostid are ignored
		//   for this check, only look at those whose m_hostId is -1
		if(pn != s_headNode && pn->m_hostId==-1){char *xx=NULL;*xx=0; }

		// a new head
		if ( pn == s_headNode ) {
			// sanity
			if ( pn->m_prevNode ) { char *xx=NULL;*xx=0; }
			// the guy after us is the new head
			s_headNode = pn->m_nextNode;
		}

		// a new tail?
		if ( pn == s_tailNode ) {
			// sanity
			if ( pn->m_nextNode ) { char *xx=NULL;*xx=0; }
			// the guy before us is the new tail
			s_tailNode = pn->m_prevNode;
		}

		// empty?
		if ( ! s_headNode ) s_tailNode = NULL;

		// wtf?
		if ( ! pn->m_calledCallback ) { char *xx=NULL;*xx=0; }

		// do callback first before freeing pn
		//if ( pn->m_callback ) pn->m_callback ( pn->m_state );

		if ( pn->m_prevNode ) 
			pn->m_prevNode->m_nextNode = pn->m_nextNode;

		if ( pn->m_nextNode )
			pn->m_nextNode->m_prevNode = pn->m_prevNode;

		mfree ( pn , sizeof(ParmNode) , "pndfr");
	}

	// try to send more for him
	g_parms.doParmSendingLoop();
}

void parmLoop ( int fd , void *state ) {
	g_parms.doParmSendingLoop();
}

static bool s_registeredSleep = false;
static bool s_inLoop = false;

// . host #0 runs this to send out parms in the the parm queue (linked list)
//   to all other hosts.
// . he also sends to himself, if m_sendToGrunts is true
bool Parms::doParmSendingLoop ( ) {

	if ( ! s_headNode ) return true;

	if ( s_inLoop ) return true;

	s_inLoop = true;

	if ( ! s_registeredSleep &&
	     ! g_loop.registerSleepCallback(2000,NULL,parmLoop,0) )
		log("parms: failed to reg parm loop");

	// do not re-register
	s_registeredSleep = true;

	long now = getTimeLocal();

	// try to send a parm update request to each host
	for ( long i = 0 ; i < g_hostdb.m_numHosts ; i++ ) {
		// get it
		Host *h = g_hostdb.getHost(i);
		// skip ourselves, host #0. we now send to ourselves
		// so updateParm() will be called on us...
		//if ( h->m_hostId == g_hostdb.m_myHostId ) continue;
		// . if in progress, gotta wait for that to complete
		// . 0 is not a legit parmid, it starts at 1
		if ( h->m_currentParmIdInProgress ) continue;
		// if his last completed parmid is the current he is uptodate
		if ( h->m_lastParmIdCompleted == s_parmId ) continue;
		// if last try had an error, wait 10 secs i guess
		if ( h->m_lastTryError &&
		     h->m_lastTryError != EUDPTIMEDOUT &&
		     now - h->m_lastTryTime < 10 )
			continue;
		// otherwise get him the next to send
		ParmNode *pn = s_headNode;
		for ( ; pn ; pn = pn->m_nextNode ) {
			// stop when we got a parmnode we have not sent to
			// him yet, we'll send it now
			if ( pn->m_parmId > h->m_lastParmIdCompleted ) break;
		}
		// nothing? strange. something is not right.
		if ( ! pn ) { 
			log("pn is null");
			break;
			char *xx=NULL; *xx=0; 
		}

		// give him a free pass? some parm updates are directed to 
		// a single host, we use this for syncing parms at startup.
		if ( pn->m_hostId >= 0 && 
		     pn->m_hostId2 == -1 && // not a range
		     h->m_hostId != pn->m_hostId ) {
			// assume we sent it to him
			h->m_lastParmIdCompleted = pn->m_parmId;
			h->m_currentNodePtr = NULL;
			continue;
		}

		// range? if not in range, give free pass
		if ( pn->m_hostId >= 0 && 
		     pn->m_hostId2 >= 0 &&
		     ( h->m_hostId < pn->m_hostId ||
		       h->m_hostId > pn->m_hostId2 ) ) {
			// assume we sent it to him
			h->m_lastParmIdCompleted = pn->m_parmId;
			h->m_currentNodePtr = NULL;
			continue;
		}

			
		// force completion if we should NOT send to him
		if ( (h->isProxy() && ! pn->m_sendToProxies) ||
		     (h->isGrunt() && ! pn->m_sendToGrunts ) ) {
			h->m_lastParmIdCompleted = pn->m_parmId;
			h->m_currentNodePtr = NULL;
			continue;
		}

		// debug log
		log("parms: sending parm request to hostid %li",h->m_hostId);

		// count it
		pn->m_numRequests++;
		// ok, he's available
		if ( ! g_udpServer.sendRequest ( pn->m_parmList.getBufStart(),
						 pn->m_parmList.length() ,
						 // a new msgtype
						 0x3f,
						 h->m_ip, // ip
						 h->m_port, // port
						 h->m_hostId ,
						 NULL, // retslot
						 (void *)h->m_hostId , // state
						 gotParmReplyWrapper ,
						 30 , // timeout secs
						 -1 , // backoff
						 -1 , // maxwait
						 NULL , // replybuf
						 0 , // replybufmaxsize
						 0 ) ) { // niceness
			log("parms: faild to send: %s",mstrerror(g_errno));
			continue;
		}
		// flag this
		h->m_currentParmIdInProgress = pn->m_parmId;
		h->m_currentNodePtr = pn;
	}

	s_inLoop = false;

	return true;
}

void handleRequest3fLoop ( void *weArg ) ;

void handleRequest3fLoop2 ( void *state , UdpSlot *slot ) {
	handleRequest3fLoop(state);
}

// if a tree is saving while we are trying to delete a collnum (or reset)
// then the call to updateParm() below returns false and we must re-call
// in this sleep wrapper here
void handleRequest3fLoop3 ( int fd , void *state ) {
	g_loop.unregisterSleepCallback(state,handleRequest3fLoop3);
	handleRequest3fLoop(state);	
}

// . host #0 is requesting that we update some parms
void handleRequest3fLoop ( void *weArg ) {
	WaitEntry *we = (WaitEntry *)weArg;

	CollectionRec *cx = NULL;

	// process them
	char *p = we->m_parmPtr;
	for ( ; p < we->m_parmEnd ; ) {
		// shortcut
		char *rec = p;
		// get size
		long dataSize = *(long *)(rec+sizeof(key96_t));
		long recSize = sizeof(key96_t) + 4 + dataSize;
		// skip it
		p += recSize;

		// get the actual parm
		Parm *parm = getParmFromParmRec ( rec );

		if ( ! parm ) {
			long h32 = getHashFromParmRec(rec);
			log("parms: unknown parm sent to us hash=%li",h32);
			for ( long i = 0 ; i < g_parms.m_numParms ; i++ ) {
				Parm *x = &g_parms.m_parms[i];
				if ( x->m_cgiHash != h32 ) continue;
				log("parms: unknown parm=%s",x->m_title);
				break;
			}
			continue;
		}

		// if was the cmd to save & exit then first send a reply back
		if ( ! we->m_sentReply &&
		     parm->m_cgi && 
		     parm->m_cgi[0] == 's' &&
		     parm->m_cgi[1] == 'a' &&
		     parm->m_cgi[2] == 'v' &&
		     parm->m_cgi[3] == 'e' &&
		     parm->m_cgi[4] == '\0' ) {
			// do not re-do this
			we->m_sentReply = 1;
			// note it
			log("parms: sending early parm update reply");
			// wait for reply to be sent and ack'd
			g_udpServer.sendReply_ass ( NULL,0,
						    NULL,0,
						    we->m_slot,
						    8, // timeout in secs
						    // come back here when done
						    we ,
						    handleRequest3fLoop2 );
			return;
		}


		// . determine if it alters the url filters
		// . if those were changed we have to nuke doledb and
		//   waiting tree in Spider.cpp and rebuild them!
		if ( parm->m_flags & PF_REBUILDURLFILTERS )
			we->m_doRebuilds = true;

		// get collnum i guess
		if ( parm->m_type != TYPE_CMD )
			we->m_collnum = getCollnumFromParmRec ( rec );

		// see if our spider round changes
		long oldRound; 
		if ( we->m_collnum >= 0 && ! cx ) {
			cx = g_collectiondb.getRec ( we->m_collnum );
			// i guess coll might gotten deleted! so check cx
			if ( cx ) oldRound = cx->m_spiderRoundNum;
		}

		// . this returns false if blocked, returns true and sets
		//   g_errno on error
		// . it'll block if trying to delete a coll when the tree
		//   is saving or something (CommandDeleteColl())
		if ( ! g_parms.updateParm ( rec , we ) ) {
			////////////
			//
			// . it blocked! it will call we->m_callback when done
			// . we must re-call
			// . try again in 100ms
			//
			////////////
			if(!g_loop.registerSleepCallback(100, 
							 we ,
							 handleRequest3fLoop3,
							 0 ) ){// niceness
				log("parms: failed to reg sleeper");
				return;
			}
			return;
		}

		if ( cx && oldRound != cx->m_spiderRoundNum )
			we->m_updatedRound = true;

		// do the next parm
		we->m_parmPtr = p;

		// error?
		if ( ! g_errno ) continue;
		// this could mean failed to add coll b/c out of disk or
		// something else that is bad
		we->m_errno = g_errno;
	}

	// one last thing... kinda hacky. if we change certain spidering parms
	// we have to do a couple rebuilds.

	// reset page round counts
	if ( we->m_updatedRound && cx ) {
		// Spider.cpp will reset the *ThisRound page counts and
		// the sent notification flag
		spiderRoundIncremented ( cx );
	}

	// basically resetting the spider here...
	if ( we->m_doRebuilds && cx ) {
		// . this tells Spider.cpp to rebuild the spider queues
		// . this is NULL if spider stuff never initialized yet,
		//   like if you just added the collection
		if ( cx->m_spiderColl )
			cx->m_spiderColl->m_waitingTreeNeedsRebuild = true;
		// . assume we have urls ready to spider too
		// . no, because if they change the filters and there are
		//   still no urls to spider i don't want to get another
		//   email alert!!
		//cr->m_localCrawlInfo .m_hasUrlsReadyToSpider = true;
		//cr->m_globalCrawlInfo.m_hasUrlsReadyToSpider = true;
		// . reconstruct the url filters if we were a custom crawl
		// . this is used to abstract away the complexity of url
		//   filters in favor of simple regular expressions and
		//   substring matching for diffbot
		cx->rebuildUrlFilters();
	}

	// note it
	if ( ! we->m_sentReply )
		log("parms: sending parm update reply");

	// send back reply now. empty reply for the most part
	if ( we->m_errno && ! we->m_sentReply )
		g_udpServer.sendErrorReply ( we->m_slot,we->m_errno,0 );
	else if ( ! we->m_sentReply )
		g_udpServer.sendReply_ass ( NULL,0,NULL,0,we->m_slot);
	// all done
	mfree ( we , sizeof(WaitEntry) , "weparm" );
	return;
}

// . host #0 is requesting that we update some parms
// . the readbuf in the request is the list of the parms
void handleRequest3f ( UdpSlot *slot , long niceness ) {

	// sending to host #0 is not right...
	//if ( g_hostdb.m_hostId == 0 ) { char *xx=NULL;*xx=0; }

	char *parmRecs = slot->m_readBuf;
	char *parmEnd  = parmRecs + slot->m_readBufSize;

	log("parms: got parm update request. size=%li.",
	    (long)(parmEnd-parmRecs));

	// make a new waiting entry
	WaitEntry *we ;
	we = (WaitEntry *) mmalloc ( sizeof(WaitEntry),"weparm");
	if ( ! we ) {
		g_udpServer.sendErrorReply(slot,g_errno,60);
		return;
	}
	we->m_slot = slot;
	we->m_callback = handleRequest3fLoop;
	we->m_parmPtr = parmRecs;
	we->m_parmEnd = parmEnd;
	we->m_errno = 0;
	we->m_doRebuilds = false;
	we->m_updatedRound = false;
	we->m_collnum = -1;
	we->m_sentReply = 0;

	handleRequest3fLoop ( we );
}




////
//
// functions for syncing parms with host #0
//
////

// 1. we do not accept any recs into rdbs until in sync with host #0
// 2. at startup we send the hash of all parms for each collrec and
//    for g_conf (collnum -1) to host #0, then he will send us all the
//    parms for a collrec (or g_conf) if we are out of sync.
// 3. when host #0 changes a parm it lets everyone know via broadcastParmList()
// 4. only host #0 may initiate parm changes. so don't let that go down!
// 5. once in sync a host can drop recs for collnums that are invalid
// 6. until in parm sync with host #0 reject adds to collnums we don't 
//    have with ETRYAGAIN in Msg4.cpp


// host #0 just sends back an empty reply, but it will hit us with
// 0x3f parmlist requests. that way it uses the same mechanism and can
// guarantee ordering of the parm update requests
void gotReplyFromHost0Wrapper ( void *state , UdpSlot *slot ) {
	// ignore his reply unless error?
	if ( g_errno )
		log("parms: got error syncing with host 0: %s",
		    mstrerror(g_errno));
	g_errno = 0;
}
	
// returns false and sets g_errno on error, true otherwise
bool Parms::syncParmsWithHost0 ( ) {

	m_inSyncWithHost0 = false;

	// dont sync with ourselves
	if ( g_hostdb.m_hostId == 0 ) {
		m_inSyncWithHost0 = true;
		return true;
	}

	// only grunts for now can sync, not proxies, so stop if we are proxy
	if ( g_hostdb.m_myHost->m_type != HT_GRUNT ) {
		m_inSyncWithHost0 = true;
		return true;
	}


	SafeBuf hashList;
	
	if ( ! makeSyncHashList ( &hashList ) ) return false;

	// copy for sending
	SafeBuf sendBuf;
	if ( ! sendBuf.safeMemcpy ( &hashList ) ) return false;
	if ( sendBuf.getCapacity() != hashList.length() ){char *xx=NULL;*xx=0;}
	if ( sendBuf.length() != hashList.length()  ){char *xx=NULL;*xx=0;}

	// allow udpserver to free it
	char *request = sendBuf.getBufStart();
	long  requestLen = sendBuf.length();
	sendBuf.detachBuf();

	Host *h = g_hostdb.getHost(0);

	// . send it off. use 3e i guess
	// . host #0 will reply using msg4 really
	// . msg4 guarantees ordering of requests
	// . there will be a record that is CMD_INSYNC so when we get
	//   that we set g_parms.m_inSyncWithHost0 to true
	if ( ! g_udpServer.sendRequest ( request ,//hashList.getBufStart() ,
					 requestLen, //hashList.length() ,
					 0x3e , // msgtype
					 h->m_ip, // ip
					 h->m_port, // port
					 h->m_hostId , // hostid , host #0!!!
					 NULL, // retslot
					 NULL , // state
					 gotReplyFromHost0Wrapper ,
					 99999999 ) ) { // timeout in secs
		log("parms: error syncing with host 0: %s",mstrerror(g_errno));
		return false;
	}

	// wait now
	return true;
}

// . here host #0 is receiving a sync request from another host
// . host #0 scans this list of hashes to make sure the requesting host is
//   in sync
// . host #0 will broadcast parm updates by calling broadcastParmList() which
//   uses 0x3f, so this just returns and empty reply on success
// . sends CMD "addcoll" and "delcoll" cmd parms as well
// . include an "insync" command parm as last parm
void handleRequest3e ( UdpSlot *slot , long niceness ) {

	// right now we must be host #0
	if ( g_hostdb.m_hostId != 0 ) { 
		g_errno = EBADENGINEER; 
	hadError:
		g_udpServer.sendErrorReply(slot,g_errno,60);
		return;
	}

	//
	// 0. scan our collections and clear a flag
	//
	for ( long i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) {
		// skip if empty
		CollectionRec *cr = g_collectiondb.m_recs[i];
		if ( ! cr ) continue;
		// clear flag
		cr->m_hackFlag = 0;
	}

	Host *host = slot->m_host;
	long hostId = -1;
	if ( host ) hostId = host->m_hostId;

	SafeBuf replyBuf;

	//
	// 1. update parms on collections we both have
	// 2. tell him to delete collections we do not have but he does
	//
	SafeBuf tmp;
	char *p = slot->m_readBuf;
	char *pend = p + slot->m_readBufSize;
	for ( ; p < pend ; ) {
		// get collnum
		collnum_t c = *(collnum_t *)p;
		p += sizeof(collnum_t);
		// sanity check. -1 means g_conf. i guess.
		if ( c < -1 ) { char *xx=NULL;*xx=0; }
		// and parm hash
		long long h64 = *(long long *)p;
		p += 8;
		// if we being host #0 do not have this collnum tell 
		// him to delete it!
		CollectionRec *cr = NULL;
		if ( c >= 0 ) cr = g_collectiondb.getRec ( c );
		if ( c >= 0 && ! cr ) {
			// note in log
			logf(LOG_INFO,"sync: telling host #%li to delete "
			     "collnum %li", hostId,(long)c);
			// add the parm rec as a parm cmd
			if (! g_parms.addNewParmToList1( &replyBuf,
							 c,
							 NULL,
							 -1,
							 "delete"))
				goto hadError;
			// ok, get next collection hash
			continue;
		}
		// set our hack flag so we know he has this collection
		if ( cr ) cr->m_hackFlag = 1;
		// get our parmlist for that collnum
		tmp.reset();
		// c is -1 for g_conf
		if ( ! g_parms.addAllParmsToList ( &tmp, c ) ) goto hadError;
		// get checksum of that
		long long m64 = hash64 ( tmp.getBufStart(),tmp.length() );
		// if match, keep chugging, that's in sync
		if ( h64 == m64 ) continue;
		// note in log
		logf(LOG_INFO,"sync: sending all parms for collnum %li "
		     "to host #%li", (long)c, hostId);
		// otherwise, send him the list
		if ( ! replyBuf.safeMemcpy ( &tmp ) ) goto hadError;
	}

	//
	// 3. now if he's missing one of our collections tell him to add it
	//
	for ( long i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) {
		// skip if empty
		CollectionRec *cr = g_collectiondb.m_recs[i];
		if ( ! cr ) continue;
		// clear flag
		if ( cr->m_hackFlag ) continue;
		char *cmdStr = "addColl";
		if ( cr->m_isCustomCrawl == 1 ) cmdStr = "addCrawl";
		if ( cr->m_isCustomCrawl == 2 ) cmdStr = "addBulk";
		// note in log
		logf(LOG_INFO,"sync: telling host #%li to add "
		     "collnum %li", hostId,(long)cr->m_collnum);
		// add the parm rec as a parm cmd
		if ( ! g_parms.addNewParmToList1 ( &replyBuf,
						   (collnum_t)i,
						   cr->m_coll, // parm val
						   -1,
						   cmdStr ) )
			goto hadError;
		// and the parmlist for it
		if (!g_parms.addAllParmsToList (&replyBuf, i ) ) goto hadError;
	}

	// . final parm is the in sync stamp of approval which will set
	//   g_parms.m_inSyncWithHost0 to true. CommandInSync()
	// .  use -1 for collnum for this cmd
	if ( ! g_parms.addNewParmToList1 ( &replyBuf,-1,NULL,-1,"insync"))
		goto hadError;

	// this should at least have the in sync command
	log("parms: sending %li bytes of parms to sync to host #%li",
	    replyBuf.length(),hostId);

	// . use the broadcast call here so things keep their order!
	// . we do not need a callback when they have been completely
	//   broadcasted to all hosts so use NULL for that
	// . crap, we only want to send this to host #x ...
	g_parms.broadcastParmList ( &replyBuf , NULL , NULL , 
				    true , // sendToGrunts?
				    false ,  // sendToProxies?
				    hostId );

	// but do send back an empty reply to this 0x3e request
	g_udpServer.sendReply_ass ( NULL,0,NULL,0,slot);
	
	// send that back now
	//g_udpServer.sendReply_ass ( replyBuf.getBufStart() ,
	//			    replyBuf.length() ,
	//			    replyBuf.getBufStart() ,
	//			    replyBuf.getCapacity() ,
	//			    slot );
	// udpserver will free it
	//replyBuf.detachBuf();
}


// get the hash of every collection's parmlist
bool Parms::makeSyncHashList ( SafeBuf *hashList ) {
	SafeBuf tmp;

	// first do g_conf, collnum -1!
	for ( long i = -1 ; i < g_collectiondb.m_numRecs ; i++ ) {
		// skip if empty
		if ( i >=0 && ! g_collectiondb.m_recs[i] ) continue;
		// clear since last time
		tmp.reset();
		// g_conf?
		if ( ! addAllParmsToList ( &tmp , i ) )
			return false;
		// store collnum first as 4 bytes
		if ( ! hashList->safeMemcpy ( &i , sizeof(collnum_t) ) )
			return false;
		// hash that shit
		long long h64 = hash64 ( tmp.getBufStart(),tmp.length() );
		// and store it
		if ( ! hashList->pushLongLong ( h64 ) )
			return false;
	}
	return true;
}

long Parm::getNumInArray ( collnum_t collnum ) {
	char *obj = (char *)&g_conf;
	if ( m_obj == OBJ_COLL ) {
		CollectionRec *cr = g_collectiondb.getRec ( collnum );
		if ( ! cr ) return -1;
		obj = (char *)cr;
	}
	// # in array is before it
	return *(long *)(obj+m_off-4);
}

// . we use this for syncing parms between hosts
// . called by convertAllCollRecsToParmList
// . returns false and sets g_errno on error
// . "rec" can be CollectionRec or g_conf ptr
bool Parms::addAllParmsToList ( SafeBuf *parmList, collnum_t collnum ) {

	// loop over parms
	for ( long i = 0 ; i < m_numParms ; i++ ) {
		// get it
		Parm *parm = &m_parms[i];
		// skip comments
		if ( parm->m_type == TYPE_COMMENT ) continue;
		// cmds
		if ( parm->m_type == TYPE_CMD ) continue;
		if ( parm->m_type == TYPE_BOOL2 ) continue;

		// daily merge last started. do not sync this...
		if ( parm->m_type == TYPE_LONG_CONST ) continue;

		if ( collnum == -1 && parm->m_obj != OBJ_CONF ) continue;
		if ( collnum >=  0 && parm->m_obj != OBJ_COLL ) continue;
		if ( collnum < -1 ) { char *xx=NULL;*xx=0; }

		// like 'statsdb max cache mem' etc.
		if ( parm->m_flags & PF_NOSYNC ) continue;

		// sanity, need cgi hash to look up the parm on the 
		// receiving end
		if ( parm->m_cgiHash == 0 ) { 
			log("parms: no cgi for parm %s",parm->m_title);
			char *xx=NULL; *xx=0;
		}

		long occNum = -1;
		long maxOccNum = 0;

		if ( parm->isArray() ) {
			maxOccNum = parm->getNumInArray(collnum) ;
			occNum = 0;
		}

		for ( ; occNum < maxOccNum ; occNum ++ ) {
			// add each occ # to list
			if ( ! addCurrentParmToList2 ( parmList ,
						       collnum ,
						       occNum ,
						       parm ) )
				return false;
			/*
			  //
			  // use this to debug parm list checksums being off
			  //
			long long h64 ;
			h64 = hash64 ( parmList->getBufStart(),
				       parmList->length() );
			// note it for debugging hash
			SafeBuf xb;
			parm->printVal ( &xb ,collnum,occNum);
			log("parms: adding (h=%llx) parm %s = %s",
			    h64,parm->m_title,xb.getBufStart());
			*/
		}

	}
	return true;
}	

// . this adds the key if not a cmd key to parmdb rdbtree
// . this executes cmds
// . this updates the CollectionRec which may disappear later and be fully
//   replaced by Parmdb, just an RdbTree really.
// . returns false if blocked
// . returns true and sets g_errno on error
bool Parms::updateParm ( char *rec , WaitEntry *we ) {

	collnum_t collnum = getCollnumFromParmRec ( rec );

	g_errno = 0;

	Parm *parm = getParmFromParmRec ( rec );

	if ( ! parm ) {
		log("parmdb: could not find parm for rec");
		g_errno = EBADENGINEER;
		return true;
	}

	// cmd to execute?
	if ( parm->m_type == TYPE_CMD ) {
		// all parm rec data for TYPE_CMD should be ascii/utf8 chars
		// and should be \0 terminated
		char *data = getDataFromParmRec ( rec );
		long dataSize = getDataSizeFromParmRec ( rec );
		if ( dataSize == 0 ) data = NULL;
		log("parmdb: running function for "
		    "parm \"%s\" (collnum=%li) args=\"%s\""
		    , parm->m_title
		    , (long)collnum
		    , data 
		    );

		// sets g_errno on error
		if ( parm->m_func ) {
			parm->m_func ( rec );
			return true;
		}

		// . returns true and sets g_errno on error
		// . returns false if blocked
		// . this is for CommandDeleteColl() and CommandResetColl()
		if ( parm->m_func2 ( rec , we ) ) return true;

		// . it did not complete.
		// . we need to re-call it using sleep wrapper above
		return false;
	}

	// "cr" will remain null when updating g_conf and collnum -1
	CollectionRec *cr = NULL;
	if ( collnum >= 0 ) {
		cr = g_collectiondb.getRec ( collnum );
		if ( ! cr ) {
			log("parmdb: invalid collnum for parm");
			g_errno = ENOCOLLREC;
			return true;
		}
	}

	// what are we updating?
	void *base = NULL;

	// we might have a collnum specified even if parm is global,
	// maybe there are some collection/local parms specified as well
	// that that collnum applies to
	if ( parm->m_obj == OBJ_COLL ) base = cr;
	else                           base = &g_conf;

	if ( ! base ) {
		log("parms: no collrec (%li) to change parm",(long)collnum);
		g_errno = ENOCOLLREC;
		return true;
	}
		
	long occNum = getOccNumFromParmRec ( rec );

	// get data
	long dataSize = *(long *)(rec+sizeof(key96_t));
	char *data = rec+sizeof(key96_t)+4;

	// point to where to copy the data into collrect
	char *dst = (char *)base + parm->m_off;
	// array?
	if ( parm->isArray() ) {
		if ( occNum < 0 ) {
			log("parms: bad occnum for %s",parm->m_title);
			return false;
		}
		// the long before the array is the # of elements
		long currentCount = *((long *)(dst-4));
		// update our # elements in our array if this is bigger
		long newCount = occNum + 1;
		if ( newCount > currentCount ) *((long *)(dst-4)) = newCount;
		// now point "dst" to the occNum-th element
		dst += parm->m_size * occNum;
	}

	//
	// compare parm to see if it changed value
	//
	SafeBuf val1;
	parm->printVal ( &val1 , collnum , occNum );

	// if parm is a safebuf...
	if ( parm->m_type == TYPE_SAFEBUF ) {
		// point to it
		SafeBuf *sb = (SafeBuf *)dst;
		// nuke it
		sb->purge();
		// require that the \0 be part of the update i guess
		//if ( ! data || dataSize <= 0 ) { char *xx=NULL;*xx=0; }
		// check for \0
		if ( data && dataSize > 0 ) {
			if ( data[dataSize-1] != '\0') { char *xx=NULL;*xx=0; }
			// this means that we can not use string POINTERS as 
			// parms!! don't include \0 as part of length
			sb->safeStrcpy ( data ); // , dataSize );
			// ensure null terminated
			sb->nullTerm();
		}
		//return true;
		// sanity
		// we no longer include the \0 in the dataSize... so a dataSize
		// of 0 means empty string...
		//if ( data[dataSize-1] != '\0' ) { char *xx=NULL;*xx=0; }
	}
	else {
		// and copy the data into collrec or g_conf
		memcpy ( dst , data , dataSize );
	}

	SafeBuf val2;
	parm->printVal ( &val2 , collnum , occNum );

	// all done if value was unchanged
	if ( strcmp ( val1.getBufStart() , val2.getBufStart() ) == 0 )
		return true;

	// show it
	log("parms: updating parm \"%s\" "
	    "(%s[%li]) (collnum=%li) from \"%s\" -> \"%s\"",
	    parm->m_title,
	    parm->m_cgi,
	    occNum,
	    (long)collnum,
	    val1.getBufStart(),
	    val2.getBufStart());

	if ( cr ) cr->m_needsSave = true;

	// all done
	return true;
}

bool Parm::printVal ( SafeBuf *sb , collnum_t collnum , long occNum ) {

	CollectionRec *cr = NULL;
	if ( collnum >= 0 ) cr = g_collectiondb.getRec ( collnum );

	char *base;
	if ( m_obj == OBJ_COLL ) base = (char *)cr;
	else                     base = (char *)&g_conf;

	if ( ! base ) {
		log("parms: no collrec (%li) to change parm",(long)collnum);
		g_errno = ENOCOLLREC;
		return true;
	}
		
	// point to where to copy the data into collrect
	char *val = (char *)base + m_off;

	if ( isArray() && occNum < 0 ) {
		log("parms: bad occnum for %s",m_title);
		return false;
	}

	// add array index to ptr
	if ( isArray() ) val += m_size * occNum;


	if ( m_type == TYPE_SAFEBUF ) {
		// point to it
		SafeBuf *sb2 = (SafeBuf *)val;
		return sb->safePrintf("%s",sb2->getBufStart());
	}

	if ( m_type == TYPE_STRING || 
	     m_type == TYPE_STRINGBOX || 
	     m_type == TYPE_SAFEBUF ||
	     m_type == TYPE_STRINGNONEMPTY )
		return sb->safePrintf("%s",val);

	if ( m_type == TYPE_LONG || m_type == TYPE_LONG_CONST ) 
		return sb->safePrintf("%li",*(long *)val);

	if ( m_type == TYPE_DATE ) 
		return sb->safePrintf("%li",*(long *)val);

	if ( m_type == TYPE_DATE2 ) 
		return sb->safePrintf("%li",*(long *)val);

	if ( m_type == TYPE_FLOAT ) 
		return sb->safePrintf("%f",*(float *)val);

	if ( m_type == TYPE_LONG_LONG ) 
		return sb->safePrintf("%lli",*(long long *)val);

	if ( m_type == TYPE_BOOL ||
	     m_type == TYPE_BOOL2 ||
	     m_type == TYPE_CHECKBOX ||
	     m_type == TYPE_PRIORITY2 ||
	     m_type == TYPE_CHAR )
		return sb->safePrintf("%hhx",*val);

	if ( m_type == TYPE_CMD ) 
		return sb->safePrintf("CMD");

	if ( m_type == TYPE_IP )
		return sb->safePrintf("%s",iptoa(*(long *)val) );

	char *xx=NULL;*xx=0;
	return false;
}
