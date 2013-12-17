#include "gb-include.h"

#include "Collectiondb.h"
//#include "CollectionRec.h"
#include "Xml.h"
#include "Url.h"
#include "Loop.h"
#include "Spider.h"  // for calling SpiderLoop::collectionsUpdated()
#include "Posdb.h"
//#include "Indexdb.h"
#include "Datedb.h"
#include "Titledb.h"
//#include "Revdb.h"
//#include "Sections.h"
#include "Placedb.h"
#include "Tagdb.h"
#include "Catdb.h"
#include "Tfndb.h"
#include "Spider.h"
//#include "Checksumdb.h"
#include "Clusterdb.h"
#include "Spider.h"
#include "Repair.h"
#include "Users.h"
#include "Parms.h"

HashTableX g_collTable;

// a global class extern'd in .h file
Collectiondb g_collectiondb;

Collectiondb::Collectiondb ( ) {
	m_numRecs = 0;
	m_numRecsUsed = 0;
	m_lastUpdateTime = 0LL;
}

// reset rdb
void Collectiondb::reset() {
	log(LOG_INFO,"db: resetting collectiondb.");
	for ( long i = 0 ; i < m_numRecs ; i++ ) {
		if ( ! m_recs[i] ) continue;
		mdelete ( m_recs[i], sizeof(CollectionRec), "CollectionRec" ); 
		delete ( m_recs[i] );
		m_recs[i] = NULL;
	}
	m_numRecs     = 0;
	m_numRecsUsed = 0;
	g_collTable.reset();
}

bool Collectiondb::init ( bool isDump ) {
	reset();
	if ( g_isYippy ) return true;
	// reset # of recs
	//m_numRecs     = 0;
	//m_numRecsUsed = 0;
	// . now load ALL recs
	// . returns false and sets g_errno on error
	if ( ! load ( isDump ) ) return false;
	// update time
	updateTime();
	// so we don't save again
	m_needsSave = false;
	// sanity
	if ( RDB_END2 < RDB_END ) { 
		log("db: increase RDB_END2 to at least %li in "
		    "Collectiondb.h",(long)RDB_END);
		char *xx=NULL;*xx=0;
	}
	// if it set g_errno, return false
	//if ( g_errno ) return log("admin: Had init error: %s.",
	//			  mstrerror(g_errno));
	g_errno = 0;
	// otherwise, true, even if reloadList() blocked
	return true;
}

// . save to disk
// . returns false if blocked, true otherwise
bool Collectiondb::save ( ) {
	if ( g_conf.m_readOnlyMode ) return true;
	// which collection rec needs a save
	for ( long i = 0 ; i < m_numRecs ; i++ ) {
		if ( ! m_recs[i]              ) continue;
		// temp debug message
		//logf(LOG_DEBUG,"admin: SAVING collection #%li ANYWAY",i);
		if ( ! m_recs[i]->m_needsSave ) continue;
		//log(LOG_INFO,"admin: Saving collection #%li.",i);
		m_recs[i]->save ( );
	}
	// oh well
	return true;
}

bool Collectiondb::load ( bool isDump ) {
	char dname[1024];
	// MDW: sprintf ( dname , "%s/collections/" , g_hostdb.m_dir );
	sprintf ( dname , "%s" , g_hostdb.m_dir );
	Dir d; 
	d.set ( dname );
	if ( ! d.open ()) return log("admin: Could not load collection config "
				     "files.");
	// note it
	log(LOG_INFO,"db: Loading collection config files.");
	// . scan through all subdirs in the collections dir
	// . they should be like, "coll.main/" and "coll.mycollection/"
	char *f;
	while ( ( f = d.getNextFilename ( "*" ) ) ) {
		// skip if first char not "coll."
		if ( strncmp ( f , "coll." , 5 ) != 0 ) continue;
		// must end on a digit (i.e. coll.main.0)
		if ( ! is_digit (f[gbstrlen(f)-1]) ) continue;
		// point to collection
		char *coll = f + 5;
		// NULL terminate at .
		char *pp = strchr ( coll , '.' );
		if ( ! pp ) continue;
		*pp = '\0';
		// get collnum
		collnum_t collnum = atol ( pp + 1 );
		// add it
		if ( ! addExistingColl ( coll , collnum ,isDump ) )
			return false;
	}
	// note it
	log(LOG_INFO,"db: Loaded data for %li collections. Ranging from "
	    "collection #0 to #%li.",m_numRecsUsed,m_numRecs-1);
	// update the time
	updateTime();
	// don't clean the tree if just dumpin
	if ( isDump ) return true;
	// remove any nodes with illegal collnums
	Rdb *r;
	//r = g_indexdb.getRdb();
	//r->m_tree.cleanTree    ((char **)r->m_bases);
	r = g_posdb.getRdb();
	r->m_tree.cleanTree    ();//(char **)r->m_bases);
	//r = g_datedb.getRdb();
	//r->m_tree.cleanTree    ((char **)r->m_bases);

	r = g_titledb.getRdb();
	r->m_tree.cleanTree    ();//(char **)r->m_bases);
	//r = g_revdb.getRdb();
	//r->m_tree.cleanTree    ((char **)r->m_bases);
	//r = g_sectiondb.getRdb();
	//r->m_tree.cleanTree    ((char **)r->m_bases);
	//r = g_checksumdb.getRdb();
	//r->m_tree.cleanTree    ((char **)r->m_bases);
	//r = g_tfndb.getRdb();
	//r->m_tree.cleanTree    ((char **)r->m_bases);
	r = g_spiderdb.getRdb();
	r->m_tree.cleanTree    ();//(char **)r->m_bases);
	r = g_doledb.getRdb();
	r->m_tree.cleanTree    ();//(char **)r->m_bases);
	// success
	return true;
}

void Collectiondb::updateTime() {
	// get time now in milliseconds
	long long newTime = gettimeofdayInMilliseconds();
	// change it
	if ( m_lastUpdateTime == newTime ) newTime++;
	// update it
	m_lastUpdateTime = newTime;
	// we need a save
	m_needsSave = true;
}

#include "Statsdb.h"
#include "Cachedb.h"
#include "Syncdb.h"

bool Collectiondb::addExistingColl ( char *coll, 
				     collnum_t collnum ,
				     bool isDump ) {

	long i = collnum;

	// ensure does not already exist in memory
	collnum_t oldCollnum = getCollnum(coll);
	if ( oldCollnum >= 0 ) {
		g_errno = EEXIST;
		log("admin: Trying to create collection \"%s\" but "
		    "already exists in memory. Do an ls on "
		    "the working dir to see if there are two "
		    "collection dirs with the same coll name",coll);
		char *xx=NULL;*xx=0;
	}

	// create the record in memory
	CollectionRec *cr = new (CollectionRec);
	if ( ! cr ) 
		return log("admin: Failed to allocated %li bytes for new "
			   "collection record for \"%s\".",
			   (long)sizeof(CollectionRec),coll);
	mnew ( cr , sizeof(CollectionRec) , "CollectionRec" ); 


	// get the default.conf from working dir if there
	g_parms.setToDefault( (char *)cr );

	strcpy ( cr->m_coll , coll );
	cr->m_collLen = gbstrlen ( coll );
	cr->m_collnum = i;

	// point to this, so Rdb and RdbBase can reference it
	coll = cr->m_coll;

	//log("admin: loaded old coll \"%s\"",coll);

	// load if not new
	if ( ! cr->load ( coll , i ) ) {
		mdelete ( cr, sizeof(CollectionRec), "CollectionRec" ); 
		delete ( cr );
		m_recs[i] = NULL;
		return log("admin: Failed to load conf for collection "
			   "\"%s\".",coll);
	}

	return registerCollRec ( cr , isDump , false );
}

// . add a new rec
// . returns false and sets g_errno on error
// . was addRec()
// . "isDump" is true if we don't need to initialize all the rdbs etc
//   because we are doing a './gb dump ...' cmd to dump out data from
//   one Rdb which we will custom initialize in main.cpp where the dump
//   code is. like for instance, posdb.
// . "customCrawl" is 0 for a regular collection, 1 for a simple crawl
//   2 for a bulk job. diffbot terminology.
bool Collectiondb::addNewColl ( char *coll , 
				char customCrawl ,
				char *cpc , 
				long cpclen , 
				bool saveIt ,
				// Parms.cpp reserves this so it can be sure
				// to add the same collnum to every shard
				collnum_t newCollnum ) {

	// ensure coll name is legit
	char *p = coll;
	for ( ; *p ; p++ ) {
		if ( is_alnum_a(*p) ) continue;
		if ( *p == '-' ) continue;
		if ( *p == '_' ) continue; // underscore now allowed
		break;
	}
	if ( *p ) {
		g_errno = EBADENGINEER;
		log("admin: \"%s\" is a malformed collection name because it "
		    "contains the '%c' character.",coll,*p);
		return false;
	}
	// . scan for holes
	// . i is also known as the collection id
	//long i = (long)newCollnum;
	// no longer fill empty slots because if they do a reset then
	// a new rec right away it will be filled with msg4 recs not
	// destined for it. Later we will have to recycle some how!!
	//else for ( i = 0 ; i < m_numRecs ; i++ ) if ( ! m_recs[i] ) break;
	// right now we #define collnum_t short. so do not breach that!
	//if ( m_numRecs < 0x7fff ) {
	//	// set it
	//	i = m_numRecs;
	//	// claim it
	//	// we don't do it here, because we check i below and
	//	// increment m_numRecs below.
	//	//m_numRecs++;
	//}
	// TODO: scan for holes here...
	//else { 
	if ( newCollnum < 0 ) { char *xx=NULL;*xx=0; }

	// ceiling?
	//long long maxColls = 1LL<<(sizeof(collnum_t)*8);
	//if ( i >= maxColls ) {
	//	g_errno = ENOBUFS;
	//	return log("admin: Limit of %lli collection reached. "
	//		   "Collection not created.",maxColls);
	//}
	// if empty... bail, no longer accepted, use "main"
	if ( ! coll || !coll[0] ) {
		g_errno = EBADENGINEER;
		return log("admin: Trying to create a new collection "
			   "but no collection name provided. Use the \"c\" "
			   "cgi parameter to specify it.");
	}
	// or if too big
	if ( gbstrlen(coll) > MAX_COLL_LEN ) {
		g_errno = ENOBUFS;
		return log("admin: Trying to create a new collection "
			   "whose name \"%s\" of %i chars is longer than the "
			   "max of %li chars.",coll,gbstrlen(coll),
			   (long)MAX_COLL_LEN);
	}
		
	// ensure does not already exist in memory
	if ( getCollnum ( coll ) >= 0 ) {
		g_errno = EEXIST;
		char *xx=NULL;*xx=0;
		return log("admin: Trying to create collection \"%s\" but "
			   "already exists in memory.",coll);
	}
	// MDW: ensure not created on disk since time of last load
	char dname[512];
	sprintf(dname, "%scoll.%s.%li/",g_hostdb.m_dir,coll,(long)newCollnum);
	DIR *dir = opendir ( dname );
	if ( dir ) closedir ( dir );
	if ( dir ) {
		g_errno = EEXIST;
		return log("admin: Trying to create collection %s but "
			   "directory %s already exists on disk.",coll,dname);
	}

	// create the record in memory
	CollectionRec *cr = new (CollectionRec);
	if ( ! cr ) 
		return log("admin: Failed to allocated %li bytes for new "
			   "collection record for \"%s\".",
			   (long)sizeof(CollectionRec),coll);
	// register the mem
	mnew ( cr , sizeof(CollectionRec) , "CollectionRec" ); 

	// get copy collection
	CollectionRec *cpcrec = NULL;
	if ( cpc && cpc[0] ) cpcrec = getRec ( cpc , cpclen );
	if ( cpc && cpc[0] && ! cpcrec )
		log("admin: Collection \"%s\" to copy config from does not "
		    "exist.",cpc);
	// get the default.conf from working dir if there
	g_parms.setToDefault( (char *)cr );

	/*
	// the default conf file
	char tmp1[1024];
	sprintf ( tmp1 , "%sdefault.conf" , g_hostdb.m_dir );
	// . set our parms from the file.
	// . accepts OBJ_COLLECTIONREC or OBJ_CONF
	g_parms.setFromFile ( cr , NULL , tmp1 );
	*/

	// this will override all
	if ( cpcrec ) {
		// copy it, but not the timedb hashtable, etc.
		long size = (char *)&(cpcrec->m_END_COPY) - (char *)cpcrec;
		// JAB: bad memcpy - no donut!
		// this is not how objects are supposed to be copied!!!
		memcpy ( cr , cpcrec , size);
	}

	// set coll id and coll name for coll id #i
	strcpy ( cr->m_coll , coll );
	cr->m_collLen = gbstrlen ( coll );
	cr->m_collnum = newCollnum;

	// point to this, so Rdb and RdbBase can reference it
	coll = cr->m_coll;

	//
	// BEGIN NEW CODE
	//

	//
	// get token and crawlname if customCrawl is 1 or 2
	//
	char *token = NULL;
	char *crawl = NULL;
	SafeBuf tmp;
	// . return true with g_errno set on error
	// . if we fail to set a parm right we should force ourselves 
	//   out sync
	if ( customCrawl ) {
		if ( ! tmp.safeStrcpy ( coll ) ) return true;
		token = tmp.getBufStart();
		// diffbot coll name format is <token>-<crawlname>
		char *h = strchr ( tmp.getBufStart() , '-' );
		if ( ! h ) {
			log("crawlbot: bad custom collname");
			g_errno = EBADENGINEER;
			return true;
		}
		*h = '\0';
		crawl = h + 1;
		if ( ! crawl[0] ) {
			log("crawlbot: bad custom crawl name");
			g_errno = EBADENGINEER;
			return true;
		}
	}

	//log("parms: added new collection \"%s\"", collName );

	cr->m_maxToCrawl = -1;
	cr->m_maxToProcess = -1;


	if ( customCrawl ) {
		// remember the token
		cr->m_diffbotToken.set ( token );
		cr->m_diffbotCrawlName.set ( crawl );
		// bring this back
		cr->m_diffbotApiUrl.set ( "" );
		cr->m_diffbotUrlCrawlPattern.set ( "" );
		cr->m_diffbotUrlProcessPattern.set ( "" );
		cr->m_diffbotPageProcessPattern.set ( "" );
		cr->m_diffbotUrlCrawlRegEx.set ( "" );
		cr->m_diffbotUrlProcessRegEx.set ( "" );
		cr->m_spiderStatus = SP_INITIALIZING;
		// do not spider more than this many urls total. 
		// -1 means no max.
		cr->m_maxToCrawl = 100000;
		// do not process more than this. -1 means no max.
		cr->m_maxToProcess = 100000;
		// -1 means no max
		cr->m_maxCrawlRounds = -1;
		// john want's deduping on by default to avoid 
		// processing similar pgs
		cr->m_dedupingEnabled = true;
		// show the ban links in the search results. the 
		// collection name is cryptographic enough to show that
		cr->m_isCustomCrawl = customCrawl;
		cr->m_diffbotOnlyProcessIfNew = true;
		// default respider to off
		cr->m_collectiveRespiderFrequency = 0.0;
		cr->m_restrictDomain = true;
		// reset the crawl stats
		cr->m_diffbotCrawlStartTime=
			gettimeofdayInMillisecondsGlobal();
		cr->m_diffbotCrawlEndTime   = 0LL;
		// . just the basics on these for now
		// . if certain parms are changed then the url filters
		//   must be rebuilt, as well as possibly the waiting tree!!!
		cr->rebuildUrlFilters ( );
	}


	cr->m_useRobotsTxt = true;

	// reset crawler stats.they should be loaded from crawlinfo.txt
	memset ( &cr->m_localCrawlInfo , 0 , sizeof(CrawlInfo) );
	memset ( &cr->m_globalCrawlInfo , 0 , sizeof(CrawlInfo) );

	// set some defaults. max spiders for all priorities in this 
	// collection
	cr->m_maxNumSpiders = 10;

	//cr->m_needsSave = 1;

	// start the spiders!
	cr->m_spideringEnabled = true;

	// override this?
	saveIt = true;

	//
	// END NEW CODE
	//

	//log("admin: adding coll \"%s\" (new=%li)",coll,(long)isNew);

	// MDW: create the new directory
 retry22:
	if ( ::mkdir ( dname , 
		       S_IRUSR | S_IWUSR | S_IXUSR | 
		       S_IRGRP | S_IWGRP | S_IXGRP | 
		       S_IROTH | S_IXOTH ) ) {
		// valgrind?
		if ( errno == EINTR ) goto retry22;
		g_errno = errno;
		mdelete ( cr , sizeof(CollectionRec) , "CollectionRec" ); 
		delete ( cr );
		return log("admin: Creating directory %s had error: "
			   "%s.", dname,mstrerror(g_errno));
	}

	// save it into this dir... might fail!
	if ( saveIt && ! cr->save() ) {
		mdelete ( cr , sizeof(CollectionRec) , "CollectionRec" ); 
		delete ( cr );
		return log("admin: Failed to save file %s: %s",
			   dname,mstrerror(g_errno));
	}


	return registerCollRec ( cr , false , true );
}

// . called only by addNewColl() and by addExistingColl()
bool Collectiondb::registerCollRec ( CollectionRec *cr ,
				     bool isDump ,
				     bool isNew ) {


	// add m_recs[] and to hashtable
	if ( ! setRecPtr ( cr->m_collnum , cr ) )
		return false;

	bool verify = true;

	char *coll = cr->m_coll;

	// if we are doing a dump from the command line, skip this stuff
	if ( isDump ) return true;


	if ( isNew ) verify = false;


	// tell rdbs to add one, too
	//if ( ! g_indexdb.addColl    ( coll, verify ) ) goto hadError;
	if ( ! g_posdb.addColl    ( coll, verify ) ) goto hadError;
	//if ( ! g_datedb.addColl     ( coll, verify ) ) goto hadError;
	
	if ( ! g_titledb.addColl    ( coll, verify ) ) goto hadError;
	//if ( ! g_revdb.addColl      ( coll, verify ) ) goto hadError;
	//if ( ! g_sectiondb.addColl  ( coll, verify ) ) goto hadError;
	if ( ! g_tagdb.addColl      ( coll, verify ) ) goto hadError;
	//if ( ! g_catdb.addColl      ( coll, verify ) ) goto hadError;
	//if ( ! g_checksumdb.addColl ( coll, verify ) ) goto hadError;
	//if ( ! g_tfndb.addColl      ( coll, verify ) ) goto hadError;
	if ( ! g_clusterdb.addColl  ( coll, verify ) ) goto hadError;
	if ( ! g_linkdb.addColl     ( coll, verify ) ) goto hadError;
	if ( ! g_spiderdb.addColl   ( coll, verify ) ) goto hadError;
	if ( ! g_doledb.addColl     ( coll, verify ) ) goto hadError;

	// debug message
	log ( LOG_INFO, "db: verified collection \"%s\" (%li).",
	      coll,(long)cr->m_collnum);

	// tell SpiderCache about this collection, it will create a 
	// SpiderCollection class for it.
	//g_spiderCache.reset1();

	// success
	return true;

 hadError:
	log("db: error registering coll: %s",mstrerror(g_errno));
	return false;
}




bool Collectiondb::isAdmin ( HttpRequest *r , TcpSocket *s ) {
	if ( r->getLong("admin",1) == 0 ) return false;
	if ( g_conf.isMasterAdmin ( s , r ) ) return true;
	char *c = r->getString ( "c" );
	CollectionRec *cr = getRec ( c );
	if ( ! cr ) return false;
	return g_users.hasPermission ( r , PAGE_SEARCH );
	//return cr->hasPermission ( r , s );
}

/*
void savingCheckWrapper1 ( int fd , void *state ) {
	WaitEntry *we = (WaitEntry *)state;
	// no state?
	if ( ! we ) { log("colldb: we1 is null"); return; }
	// unregister too
	g_loop.unregisterSleepCallback ( state,savingCheckWrapper1 );
	// if it blocked again i guess tree is still saving
	if ( ! g_collectiondb.resetColl ( we->m_coll ,
					  we ,
					  we->m_purgeSeeds))
		return;
	// all done
	we->m_callback ( we->m_state );
}

void savingCheckWrapper2 ( int fd , void *state ) {
	WaitEntry *we = (WaitEntry *)state;
	// no state?
	if ( ! we ) { log("colldb: we2 is null"); return; }
	// unregister too
	g_loop.unregisterSleepCallback ( state,savingCheckWrapper2 );
	// if it blocked again i guess tree is still saving
	if ( ! g_collectiondb.deleteRec ( we->m_coll , we ) ) return;
	// all done
	we->m_callback ( we->m_state );
}
*/

/*
// delete all records checked in the list
bool Collectiondb::deleteRecs ( HttpRequest *r ) {
	for ( long i = 0 ; i < r->getNumFields() ; i++ ) {
		char *f = r->getField ( i );
		if ( strncmp ( f , "del" , 3 ) != 0 ) continue;
		char *coll = f + 3;
		//if ( ! is_digit ( f[3] )            ) continue;
		//long h = atol ( f + 3 );
		deleteRec ( coll , NULL );
	}
	return true;
}
*/

/*
// . delete a collection
// . this uses blocking unlinks, may make non-blocking later
// . returns false if blocked, true otherwise
bool Collectiondb::deleteRec ( char *coll , WaitEntry *we ) {
	// force on for now
	//deleteTurkdb = true;
	// no spiders can be out. they may be referencing the CollectionRec
	// in XmlDoc.cpp... quite likely.
	//if ( g_conf.m_spideringEnabled ||
	//     g_spiderLoop.m_numSpidersOut > 0 ) {
	//	log("admin: Can not delete collection while "
	//	    "spiders are enabled or active.");
	//	return false;
	//}
	// ensure it's not NULL
	if ( ! coll ) {
		log(LOG_LOGIC,"admin: Collection name to delete is NULL.");
		g_errno = ENOTFOUND;
		return true;
	}
	// find the rec for this collection
	collnum_t collnum = getCollnum ( coll );
	return deleteRec2 ( collnum , we );
}
*/

bool Collectiondb::deleteRec2 ( collnum_t collnum ) { //, WaitEntry *we ) {
	// do not allow this if in repair mode
	if ( g_repairMode > 0 ) {
		log("admin: Can not delete collection while in repair mode.");
		g_errno = EBADENGINEER;
		return true;
	}
	// bitch if not found
	if ( collnum < 0 ) {
		g_errno = ENOTFOUND;
		log(LOG_LOGIC,"admin: Collection #%li is bad, "
		    "delete failed.",(long)collnum);
		return true;
	}
	CollectionRec *cr = m_recs [ collnum ];
	if ( ! cr ) {
		log("admin: Collection id problem. Delete failed.");
		g_errno = ENOTFOUND;
		return true;
	}

	if ( g_process.isAnyTreeSaving() ) {
		// note it
		log("admin: tree is saving. waiting2.");
		// all done
		return false;
	}

	// spiders off
	//if ( cr->m_spiderColl &&
	//     cr->m_spiderColl->getTotalOutstandingSpiders() > 0 ) {
	//	log("admin: Can not delete collection while "
	//	    "spiders are oustanding for collection. Turn off "
	//	    "spiders and wait for them to exit.");
	//	return false;
	//}

	char *coll = cr->m_coll;

	// note it
	log(LOG_INFO,"db: deleting coll \"%s\" (%li)",coll,
	    (long)cr->m_collnum);

	// we need a save
	m_needsSave = true;

	// nuke doleiptable and waintree and waitingtable
	/*
	SpiderColl *sc = g_spiderCache.getSpiderColl ( collnum );
	sc->m_waitingTree.clear();
	sc->m_waitingTable.clear();
	sc->m_doleIpTable.clear();
	g_spiderLoop.m_lockTable.clear();
	g_spiderLoop.m_lockCache.clear(0);
	sc->m_lastDownloadCache.clear(collnum);
	*/

	// CAUTION: tree might be in the middle of saving
	// we deal with this in Process.cpp now

	// remove from spider cache, tell it to sync up with collectiondb
	//g_spiderCache.reset1();
	// . TODO: remove from g_sync
	// . remove from all rdbs
	//g_indexdb.getRdb()->delColl    ( coll );
	g_posdb.getRdb()->delColl    ( coll );
	//g_datedb.getRdb()->delColl     ( coll );

	g_titledb.getRdb()->delColl    ( coll );
	//g_revdb.getRdb()->delColl      ( coll );
	//g_sectiondb.getRdb()->delColl  ( coll );
	g_tagdb.getRdb()->delColl ( coll );
	// let's preserve the tags... they have all the turk votes in them
	//if ( deleteTurkdb ) {
	//}
	//g_catdb.getRdb()->delColl      ( coll );
	//g_checksumdb.getRdb()->delColl ( coll );
	g_spiderdb.getRdb()->delColl   ( coll );
	g_doledb.getRdb()->delColl     ( coll );
	//g_tfndb.getRdb()->delColl      ( coll );
	g_clusterdb.getRdb()->delColl  ( coll );
	g_linkdb.getRdb()->delColl     ( coll );

	// reset spider info
	SpiderColl *sc = g_spiderCache.getSpiderCollIffNonNull(collnum);
	if ( sc ) {
		// remove locks from lock table:
		sc->clear();
		//sc->m_collnum = newCollnum;
		sc->reset();
		mdelete ( sc, sizeof(SpiderColl),"nukecr2");
		cr->m_spiderColl = NULL;
	}

	//////
	//
	// remove from m_recs[]
	//
	//////
	setRecPtr ( cr->m_collnum , NULL );

	// free it
	mdelete ( cr, sizeof(CollectionRec),  "CollectionRec" ); 
	delete ( cr );

	// do not do this here in case spiders were outstanding 
	// and they added a new coll right away and it ended up getting
	// recs from the deleted coll!!
	//while ( ! m_recs[m_numRecs-1] ) m_numRecs--;

	// update the time
	//updateTime();
	// done
	return true;
}

//#include "PageTurk.h"

/*
// . reset a collection
// . returns false if blocked and will call callback
bool Collectiondb::resetColl ( char *coll ,  bool purgeSeeds) {

	// ensure it's not NULL
	if ( ! coll ) {
		log(LOG_LOGIC,"admin: Collection name to delete is NULL.");
		g_errno = ENOCOLLREC;
		return true;
	}

	// get the CollectionRec for "test"
	CollectionRec *cr = getRec ( coll ); // "test" );

	// must be there. if not, we create test i guess
	if ( ! cr ) { 
		log("db: could not get coll rec \"%s\" to reset", coll);
		char *xx=NULL;*xx=0; 
	}

	return resetColl2 ( cr->m_collnum, purgeSeeds);
}
*/


bool Collectiondb::setRecPtr ( collnum_t collnum , CollectionRec *cr ) {

	// first time init hashtable that maps coll to collnum
	if ( g_collTable.m_numSlots == 0 &&
	     ! g_collTable.set(8,sizeof(collnum_t), 256,NULL,0,
			       false,0,"nhshtbl"))
		return false;

	// sanity
	if ( collnum < 0 ) { char *xx=NULL;*xx=0; }

	// sanity
	long max = m_recPtrBuf.getCapacity() / sizeof(CollectionRec *);

	// set it
	m_recs = (CollectionRec **)m_recPtrBuf.getBufStart();

	// a delete?
	if ( ! cr ) {
		// sanity
		if ( collnum >= max ) { char *xx=NULL;*xx=0; }
		// get what's there
		CollectionRec *oc = m_recs[collnum];
		// let it go
		m_recs[collnum] = NULL;
		// if nothing already, done
		if ( ! oc ) return true;
		// tally it up
		m_numRecsUsed--;
		// delete key
		long long h64 = hash64n(oc->m_coll);
		// if in the hashtable UNDER OUR COLLNUM then nuke it
		// otherwise, we might be called from resetColl2()
		void *vp = g_collTable.getValue ( &h64 );
		if ( ! vp ) return true;
		collnum_t ct = *(collnum_t *)vp;
		if ( ct != collnum ) return true;
		g_collTable.removeKey ( &h64 );
		return true;
	}

	// an add, make sure big enough
	long need = ((long)collnum+1)*sizeof(CollectionRec *);
	long have = m_recPtrBuf.getLength();
	long need2 = need - have;
	// . true here means to clear the new space to zeroes
	// . this shit works based on m_length not m_capacity
	if ( need2 > 0 && ! m_recPtrBuf.reserve ( need2 ,NULL, true ) ) {
		log("admin: error growing rec ptr buf2.");
		return false;
	}

	// sanity
	if ( cr->m_collnum != collnum ) { char *xx=NULL;*xx=0; }
	// update length of used bytes in case we re-alloc
	m_recPtrBuf.setLength ( need );
	// sanity
	if ( m_recPtrBuf.getCapacity() < need ) { char *xx=NULL;*xx=0; }
	// re-ref it in case it is different
	m_recs = (CollectionRec **)m_recPtrBuf.getBufStart();
	// re-max
	max = m_recPtrBuf.getCapacity() / sizeof(CollectionRec *);
	// sanity
	if ( collnum >= max ) { char *xx=NULL;*xx=0; }

	// add to hash table to map name to collnum_t
	long long h64 = hash64n(cr->m_coll);
	// debug
	//log("coll: adding key %lli for %s",h64,cr->m_coll);
	if ( ! g_collTable.addKey ( &h64 , &collnum ) ) 
		return false;

	// ensure last is NULL
	m_recs[collnum] = cr;

	// count it
	m_numRecsUsed++;

	//log("coll: adding key4 %llu for coll \"%s\" (%li)",h64,cr->m_coll,
	//    (long)i);

	// reserve it
	if ( collnum >= m_numRecs ) m_numRecs = collnum + 1;

	// sanity to make sure collectionrec ptrs are legit
	for ( long j = 0 ; j < m_numRecs ; j++ ) {
		if ( ! m_recs[j] ) continue;
		if ( m_recs[j]->m_collnum == 1 ) continue;
	}

	// update the time
	updateTime();

	return true;
}

// . returns false if we need a re-call, true if we completed
// . returns true with g_errno set on error
bool Collectiondb::resetColl2( collnum_t oldCollnum,
			       collnum_t newCollnum,
			       //WaitEntry *we,
			       bool purgeSeeds){

	// save parms in case we block
	//we->m_purgeSeeds = purgeSeeds;

	// now must be "test" only for now
	//if ( strcmp(coll,"test") ) { char *xx=NULL;*xx=0; }
	// no spiders can be out. they may be referencing the CollectionRec
	// in XmlDoc.cpp... quite likely.
	//if ( g_conf.m_spideringEnabled ||
	//     g_spiderLoop.m_numSpidersOut > 0 ) {
	//	log("admin: Can not delete collection while "
	//	    "spiders are enabled or active.");
	//	return false;
	//}

	// do not allow this if in repair mode
	if ( g_repairMode > 0 ) {
		log("admin: Can not delete collection while in repair mode.");
		g_errno = EBADENGINEER;
		return true;
	}

	log("admin: resetting collnum %li",(long)oldCollnum);

	// CAUTION: tree might be in the middle of saving
	// we deal with this in Process.cpp now
	if ( g_process.isAnyTreeSaving() ) {
		// we could not complete...
		return false;
	}

	CollectionRec *cr = m_recs [ oldCollnum ];

	// let's reset crawlinfo crap
	cr->m_globalCrawlInfo.reset();
	cr->m_localCrawlInfo.reset();

	//collnum_t oldCollnum = cr->m_collnum;
	//collnum_t newCollnum = m_numRecs;

	// reset spider info
	SpiderColl *sc = g_spiderCache.getSpiderCollIffNonNull(oldCollnum);
	if ( sc ) {
		sc->clear();
		sc->m_collnum = newCollnum;
	}

	// reset spider round
	cr->m_spiderRoundNum = 0;
	cr->m_spiderRoundStartTime = 0;

	cr->m_spiderStatus = SP_INITIALIZING; // this is 0
	//cr->m_spiderStatusMsg = NULL;

	// reset seed buf
	if ( purgeSeeds ) {
		// free the buffer of seed urls
		cr->m_diffbotSeeds.purge();
		// reset seed dedup table
		HashTableX *ht = &cr->m_seedHashTable;
		ht->reset();
	}

	// so XmlDoc.cpp can detect if the collection was reset since it
	// launched its spider:
	cr->m_lastResetCount++;


	if ( newCollnum >= m_numRecs ) m_numRecs = (long)newCollnum + 1;

	// advance sanity check. did we wrap around?
	// right now we #define collnum_t short
	if ( m_numRecs > 0x7fff ) { char *xx=NULL;*xx=0; }

	// make a new collnum so records in transit will not be added
	// to any rdb...
	cr->m_collnum = newCollnum;

	////////
	//
	// ALTER m_recs[] array
	//
	////////

	// Rdb::resetColl() needs to know the new cr so it can move
	// the RdbBase into cr->m_bases[rdbId] array. recycling.
	setRecPtr ( newCollnum , cr );

	// a new directory then since we changed the collnum
	char dname[512];
	sprintf(dname, "%scoll.%s.%li/",
		g_hostdb.m_dir,
		cr->m_coll,
		(long)newCollnum);
	DIR *dir = opendir ( dname );
	if ( dir )
	     closedir ( dir );
	if ( dir ) {
		//g_errno = EEXIST;
		log("admin: Trying to create collection %s but "
		    "directory %s already exists on disk.",cr->m_coll,dname);
	}
	if ( ::mkdir ( dname , 
		       S_IRUSR | S_IWUSR | S_IXUSR | 
		       S_IRGRP | S_IWGRP | S_IXGRP | 
		       S_IROTH | S_IXOTH ) ) {
		// valgrind?
		//if ( errno == EINTR ) goto retry22;
		//g_errno = errno;
		log("admin: Creating directory %s had error: "
		    "%s.", dname,mstrerror(g_errno));
	}


	// . unlink all the *.dat and *.map files for this coll in its subdir
	// . remove all recs from this collnum from m_tree/m_buckets
	// . updates RdbBase::m_collnum
	// . so for the tree it just needs to mark the old collnum recs
	//   with a collnum -1 in case it is saving...
	g_posdb.getRdb()->deleteColl     ( oldCollnum , newCollnum );
	g_titledb.getRdb()->deleteColl   ( oldCollnum , newCollnum );
	g_tagdb.getRdb()->deleteColl     ( oldCollnum , newCollnum );
	g_spiderdb.getRdb()->deleteColl  ( oldCollnum , newCollnum );
	g_doledb.getRdb()->deleteColl    ( oldCollnum , newCollnum );
	g_clusterdb.getRdb()->deleteColl ( oldCollnum , newCollnum );
	g_linkdb.getRdb()->deleteColl    ( oldCollnum , newCollnum );

	// reset crawl status too!
	cr->m_spiderStatus = SP_INITIALIZING;

	// . set m_recs[oldCollnum] to NULL and remove from hash table
	// . do after calls to deleteColl() above so it wont crash
	setRecPtr ( oldCollnum , NULL );


	// save coll.conf to new directory
	cr->save();


	// and clear the robots.txt cache in case we recently spidered a
	// robots.txt, we don't want to use it, we want to use the one we
	// have in the test-parser subdir so we are consistent
	RdbCache *robots = Msg13::getHttpCacheRobots();
	RdbCache *others = Msg13::getHttpCacheOthers();
	robots->clear ( oldCollnum );
	others->clear ( oldCollnum );

	//g_templateTable.reset();
	//g_templateTable.save( g_hostdb.m_dir , "turkedtemplates.dat" );

	// repopulate CollectionRec::m_sortByDateTable. should be empty
	// since we are resetting here.
	//initSortByDateTable ( coll );

	// done
	return true;
}

// get coll rec specified in the HTTP request
CollectionRec *Collectiondb::getRec ( HttpRequest *r ) {
	char *coll = r->getString ( "c" );
	if ( coll && ! coll[0] ) coll = NULL;
	// maybe it is crawlbot?
	char *name = NULL;
	char *token = NULL;
	if ( ! coll ) {
		name = r->getString("name");
		token = r->getString("token");
	}
	char tmp[MAX_COLL_LEN+1];
	if ( ! coll && token && name ) {
		snprintf(tmp,MAX_COLL_LEN,"%s-%s",token,name);
		coll = tmp;
	}
	// give up?
	if ( ! coll ) return NULL;
	//if ( ! coll || ! coll[0] ) coll = g_conf.m_defaultColl;
	return g_collectiondb.getRec ( coll );
}

// . get collectionRec from name
// . returns NULL if not available
CollectionRec *Collectiondb::getRec ( char *coll ) {
	if ( ! coll ) coll = "";
	return getRec ( coll , gbstrlen(coll) );
}

CollectionRec *Collectiondb::getRec ( char *coll , long collLen ) {
	if ( ! coll ) coll = "";
	collnum_t collnum = getCollnum ( coll , collLen );
	if ( collnum < 0 ) return NULL;
	return m_recs [ (long)collnum ]; 
}

CollectionRec *Collectiondb::getRec ( collnum_t collnum) { 
	if ( collnum >= m_numRecs || collnum < 0 ) {
		// Rdb::resetBase() gets here, so don't always log.
		// it is called from CollectionRec::reset() which is called
		// from the CollectionRec constructor and ::load() so
		// it won't have anything in rdb at that time
		//log("colldb: collnum %li > numrecs = %li",
		//    (long)collnum,(long)m_numRecs);
		return NULL;
	}
	return m_recs[collnum]; 
}


//CollectionRec *Collectiondb::getDefaultRec ( ) {
//	if ( ! g_conf.m_defaultColl[0] ) return NULL; // no default?
//	collnum_t collnum = getCollnum ( g_conf.m_defaultColl );	
//	if ( collnum < (collnum_t)0 ) return NULL;
//	return m_recs[(long)collnum];
//}

CollectionRec *Collectiondb::getFirstRec ( ) {
	for ( long i = 0 ; i < m_numRecs ; i++ )
		if ( m_recs[i] ) return m_recs[i];
	return NULL;
}

collnum_t Collectiondb::getFirstCollnum ( ) {
	for ( long i = 0 ; i < m_numRecs ; i++ )
		if ( m_recs[i] ) return i;
	return (collnum_t)-1;
}

char *Collectiondb::getFirstCollName ( ) {
	for ( long i = 0 ; i < m_numRecs ; i++ )
		if ( m_recs[i] ) return m_recs[i]->m_coll;
	return NULL;
}

char *Collectiondb::getCollName ( collnum_t collnum ) {
	if ( collnum < 0 || collnum > m_numRecs ) return NULL;
	if ( ! m_recs[(long)collnum] ) return NULL;
	return m_recs[collnum]->m_coll;
}

collnum_t Collectiondb::getCollnum ( char *coll ) {

	long clen = 0;
	if ( coll ) clen = gbstrlen(coll );
	return getCollnum ( coll , clen );
	/*
	//if ( ! coll ) coll = "";

	// default empty collection names
	if ( coll && ! coll[0] ) coll = NULL;
	if ( ! coll ) coll = g_conf.m_defaultColl;
	if ( ! coll || ! coll[0] ) coll = "main";


	// This is necessary for Statsdb to work, as it is
	// not associated with any collection. Is this
	// necessary for Catdb?
	if ( coll[0]=='s' && coll[1] =='t' &&
	     strcmp ( "statsdb\0", coll ) == 0) 
		return 0;
	if ( coll[0]=='f' && coll[1]=='a' &&
	     strcmp ( "facebookdb\0", coll ) == 0) 
		return 0;
	if ( coll[0]=='a' && coll[1]=='c' &&
	     strcmp ( "accessdb\0", coll ) == 0) 
		return 0;

	// because diffbot may have thousands of crawls/collections
	// let's improve the speed here. try hashing it...
	long long h64 = hash64n(coll);
	void *vp = g_collTable.getValue ( &h64 );
	if ( ! vp ) return -1; // not found
	return *(collnum_t *)vp;
	*/
	/*
	for ( long i = 0 ; i < m_numRecs ; i++ ) {
		if ( ! m_recs[i] ) continue;
		if ( m_recs[i]->m_coll[0] != coll[0] ) continue;
		if ( strcmp ( m_recs[i]->m_coll , coll ) == 0 ) return i;
	}
	//if ( strcmp ( "catdb\0", coll ) == 0) return 0;
	return (collnum_t)-1; // not found
	*/
}

collnum_t Collectiondb::getCollnum ( char *coll , long clen ) {

	// default empty collection names
	if ( coll && ! coll[0] ) coll = NULL;
	if ( ! coll ) {
		coll = g_conf.m_defaultColl;
		if ( coll ) clen = gbstrlen(coll);
		else clen = 0;
	}
	if ( ! coll || ! coll[0] ) {
		coll = "main";
		clen = gbstrlen(coll);
	}

	// This is necessary for Statsdb to work, as it is
	//if ( ! coll ) coll = "";

	// not associated with any collection. Is this
	// necessary for Catdb?
	if ( coll[0]=='s' && coll[1] =='t' &&
	     strcmp ( "statsdb\0", coll ) == 0) 
		return 0;
	if ( coll[0]=='f' && coll[1]=='f' &&
	     strcmp ( "facebookdb\0", coll ) == 0) 
		return 0;
	if ( coll[0]=='a' && coll[1]=='c' &&
	     strcmp ( "accessdb\0", coll ) == 0) 
		return 0;


	// because diffbot may have thousands of crawls/collections
	// let's improve the speed here. try hashing it...
	long long h64 = hash64(coll,clen);
	void *vp = g_collTable.getValue ( &h64 );
	if ( ! vp ) return -1; // not found
	return *(collnum_t *)vp;

	/*
	for ( long i = 0 ; i < m_numRecs ; i++ ) {
		if ( ! m_recs[i] ) continue;
		if ( m_recs[i]->m_collLen != clen ) continue;
		if ( strncmp(m_recs[i]->m_coll,coll,clen) == 0 ) return i;
	}

	//if ( strncmp ( "catdb\0", coll, clen ) == 0) return 0;
	return (collnum_t)-1; // not found
	*/
}

//collnum_t Collectiondb::getNextCollnum ( collnum_t collnum ) {
//	for ( long i = (long)collnum + 1 ; i < m_numRecs ; i++ ) 
//		if ( m_recs[i] ) return i;
//	// no next one, use -1
//	return (collnum_t) -1;
//}

// what collnum will be used the next time a coll is added?
collnum_t Collectiondb::reserveCollNum ( ) {

	if ( m_numRecs < 0x7fff ) {
		collnum_t next = m_numRecs;
		m_numRecs++;
		return next;
	}

	// search for an empty slot
	for ( long i = 0 ; i < m_numRecs ; i++ ) {
		if ( ! m_recs[i] ) return (collnum_t)i;
	}

	log("colldb: no new collnum available. consider upping collnum_t");
	// none available!!
	return -1;
}


///////////////
//
// COLLECTIONREC
//
///////////////

#include "gb-include.h"

//#include "CollectionRec.h"
//#include "Collectiondb.h"
#include "HttpServer.h"     // printColors2()
#include "Msg5.h"
#include "Threads.h"
#include "Datedb.h"
#include "Timedb.h"
#include "Spider.h"
#include "Process.h"

static CollectionRec g_default;


CollectionRec::CollectionRec() {
	//m_numSearchPwds = 0;
	//m_numBanIps     = 0;
	//m_numSearchIps  = 0;
	//m_numSpamIps    = 0;
	//m_numAdminPwds  = 0;
	//m_numAdminIps   = 0;
	memset ( m_bases , 0 , 4*RDB_END );
	// how many keys in the tree of each rdb? we now store this stuff
	// here and not in RdbTree.cpp because we no longer have a maximum
	// # of collection recs... MAX_COLLS. each is a 32-bit "long" so
	// it is 4 * RDB_END...
	memset ( m_numNegKeysInTree , 0 , 4*RDB_END );
	memset ( m_numPosKeysInTree , 0 , 4*RDB_END );
	m_spiderColl = NULL;
	m_overflow  = 0x12345678;
	m_overflow2 = 0x12345678;
	// the spiders are currently uninhibited i guess
	m_spiderStatus = SP_INITIALIZING; // this is 0
	//m_spiderStatusMsg = NULL;
	// for Url::getSite()
	m_updateSiteRulesTable = 1;
	m_lastUpdateTime = 0LL;
	m_clickNScrollEnabled = false;
	// inits for sortbydatetable
	m_inProgress = false;
	m_msg5       = NULL;
	// JAB - track which regex parsers have been initialized
	//log(LOG_DEBUG,"regex: %p initalizing empty parsers", m_pRegExParser);

	// clear these out so Parms::calcChecksum can work:
	memset( m_spiderFreqs, 0, MAX_FILTERS*sizeof(*m_spiderFreqs) );
	//for ( int i = 0; i < MAX_FILTERS ; i++ ) 
	//	m_spiderQuotas[i] = -1;
	memset( m_spiderPriorities, 0, 
		MAX_FILTERS*sizeof(*m_spiderPriorities) );
	//memset( m_rulesets, 0, MAX_FILTERS*sizeof(*m_rulesets) );
	//for ( int i = 0; i < MAX_SEARCH_PASSWORDS; i++ ) {
	//	*(m_searchPwds[i]) = '\0';
	//}
	//for ( int i = 0; i < MAX_ADMIN_PASSWORDS; i++ ) {
	//	*(m_adminPwds[i]) = '\0';
	//}
	//memset( m_banIps, 0, MAX_BANNED_IPS*sizeof(*m_banIps) );
	//memset( m_searchIps, 0, MAX_SEARCH_IPS*sizeof(*m_searchIps) );
	//memset( m_spamIps, 0, MAX_SPAM_IPS*sizeof(*m_spamIps) );
	//memset( m_adminIps, 0, MAX_ADMIN_IPS*sizeof(*m_adminIps) );

	//for ( int i = 0; i < MAX_FILTERS; i++ ) {
	//	//m_pRegExParser[i] = NULL;
	//	*(m_regExs[i]) = '\0';
	//}
	m_numRegExs = 0;

	//m_requests = 0;
	//m_replies = 0;
	//m_doingCallbacks = false;

	m_lastResetCount = 0;

	// regex_t types
	m_hasucr = false;
	m_hasupr = false;

	// for diffbot caching the global spider stats
	reset();

	// add default reg ex if we do not have one
	setUrlFiltersToDefaults();
}

CollectionRec::~CollectionRec() {
	//invalidateRegEx ();
        reset();
}

// new collection recs get this called on them
void CollectionRec::setToDefaults ( ) {
	g_parms.setFromFile ( this , NULL , NULL  );
	// add default reg ex
	setUrlFiltersToDefaults();
}

void CollectionRec::reset() {

	// regex_t types
	if ( m_hasucr ) regfree ( &m_ucr );
	if ( m_hasupr ) regfree ( &m_upr );

	// make sure we do not leave spiders "hanging" waiting for their
	// callback to be called... and it never gets called
	//if ( m_callbackQueue.length() > 0 ) { char *xx=NULL;*xx=0; }
	//if ( m_doingCallbacks ) { char *xx=NULL;*xx=0; }
	//if ( m_replies != m_requests  ) { char *xx=NULL;*xx=0; }
	m_localCrawlInfo.reset();
	m_globalCrawlInfo.reset();
	//m_requests = 0;
	//m_replies = 0;
	// free all RdbBases in each rdb
	for ( long i = 0 ; i < g_process.m_numRdbs ; i++ ) {
	     Rdb *rdb = g_process.m_rdbs[i];
	     rdb->resetBase ( m_collnum );
	}

}

CollectionRec *g_cr = NULL;

// . load this data from a conf file
// . values we do not explicitly have will be taken from "default",
//   collection config file. if it does not have them then we use
//   the value we received from call to setToDefaults()
// . returns false and sets g_errno on load error
bool CollectionRec::load ( char *coll , long i ) {
	// also reset some counts not included in parms list
	reset();
	// before we load, set to defaults in case some are not in xml file
	g_parms.setToDefault ( (char *)this );
	// get the filename with that id
	File f;
	char tmp2[1024];
	sprintf ( tmp2 , "%scoll.%s.%li/coll.conf", g_hostdb.m_dir , coll,i);
	f.set ( tmp2 );
	if ( ! f.doesExist () ) return log("admin: %s does not exist.",tmp2);
	// set our collection number
	m_collnum = i;
	// set our collection name
	m_collLen = gbstrlen ( coll );
	strcpy ( m_coll , coll );

	// collection name HACK for backwards compatibility
	//if ( strcmp ( coll , "main" ) == 0 ) {
	//	m_coll[0] = '\0';
	//	m_collLen = 0;
	//}

	// the default conf file
	char tmp1[1024];
	sprintf ( tmp1 , "%sdefault.conf" , g_hostdb.m_dir );

	// . set our parms from the file.
	// . accepts OBJ_COLLECTIONREC or OBJ_CONF
	g_parms.setFromFile ( this , tmp2 , tmp1 );

	// add default reg ex IFF there are no url filters there now
	if ( m_numRegExs == 0 ) setUrlFiltersToDefaults();

	// compile regexs here
	char *rx = m_diffbotUrlCrawlRegEx.getBufStart();
	if ( rx && ! rx[0] ) rx = NULL;
	if ( rx ) m_hasucr = true;
	if ( rx && regcomp ( &m_ucr , rx ,
		       REG_EXTENDED|REG_ICASE|
		       REG_NEWLINE|REG_NOSUB) ) {
			// error!
			return log("xmldoc: regcomp %s failed: %s. "
				   "Ignoring.",
				   rx,mstrerror(errno));
	}

	rx = m_diffbotUrlProcessRegEx.getBufStart();
	if ( rx && ! rx[0] ) rx = NULL;
	if ( rx ) m_hasupr = true;
	if ( rx && regcomp ( &m_upr , rx ,
		       REG_EXTENDED|REG_ICASE|
		       REG_NEWLINE|REG_NOSUB) ) {
			// error!
			return log("xmldoc: regcomp %s failed: %s. "
				   "Ignoring.",
				   rx,mstrerror(errno));
	}


	//
	// LOAD the crawlinfo class in the collectionrec for diffbot
	//
	// LOAD LOCAL
	sprintf ( tmp1 , "%scoll.%s.%li/localcrawlinfo.dat",
		  g_hostdb.m_dir , m_coll , (long)m_collnum );
	log(LOG_INFO,"db: loading %s",tmp1);
	m_localCrawlInfo.reset();
	SafeBuf sb;
	// fillfromfile returns 0 if does not exist, -1 on read error
	if ( sb.fillFromFile ( tmp1 ) > 0 )
		//m_localCrawlInfo.setFromSafeBuf(&sb);
		// it is binary now
		memcpy ( &m_localCrawlInfo , sb.getBufStart(),sb.length() );
	// LOAD GLOBAL
	sprintf ( tmp1 , "%scoll.%s.%li/globalcrawlinfo.dat",
		  g_hostdb.m_dir , m_coll , (long)m_collnum );
	log(LOG_INFO,"db: loading %s",tmp1);
	m_globalCrawlInfo.reset();
	sb.reset();
	if ( sb.fillFromFile ( tmp1 ) > 0 )
		//m_globalCrawlInfo.setFromSafeBuf(&sb);
		// it is binary now
		memcpy ( &m_globalCrawlInfo , sb.getBufStart(),sb.length() );

	// ignore errors i guess
	g_errno = 0;


	// fix for diffbot
	if ( m_isCustomCrawl ) m_dedupingEnabled = true;

	// always turn on distributed spider locking because otherwise
	// we end up calling Msg50 which calls Msg25 for the same root url
	// at the same time, thereby wasting massive resources. it is also
	// dangerous to run without this because webmaster get pissed when
	// we slam their servers.
	// This is now deprecated...
	//m_useSpiderLocks      = false;
	// and all pages downloaded from a particular ip should be done
	// by the same host in our cluster to prevent webmaster rage
	//m_distributeSpiderGet = true;

	//initSortByDateTable(m_coll);

	return true;
}

/*
bool CollectionRec::countEvents ( ) {
	// set our m_numEventsOnHost value
	log("coll: loading event count termlist gbeventcount");
	// temporarily turn off threads
	bool enabled = g_threads.areThreadsEnabled();
	g_threads.disableThreads();
	// count them
	m_numEventsOnHost = 0;
	// 1MB at a time
	long minRecSizes = 1000000;
	// look up this termlist, gbeventcount which we index in XmlDoc.cpp
	long long termId = hash64n("gbeventcount") & TERMID_MASK;
	// make datedb key from it
	key128_t startKey = g_datedb.makeStartKey ( termId , 0xffffffff );
	key128_t endKey   = g_datedb.makeEndKey ( termId , 0 );
	
	Msg5 msg5;
	RdbList list;

	// . init m_numEventsOnHost by getting the exact length of that 
	//   termlist on this host
	// . send in the ping request packet so all hosts can total up
	// . Rdb.cpp should be added to incrementally so we should have no
	//   double positives.
	// . Rdb.cpp should inspect each datedb rec for this termid in
	//   a fast an efficient manner
 loop:
	// use msg5 to get the list, should ALWAYS block since no threads
	if ( ! msg5.getList ( RDB_DATEDB    ,
			      m_coll        ,
			      &list         ,
			      (char *)&startKey      ,
			      (char *)&endKey        ,
			      minRecSizes   ,
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
			      NULL          )){// msg5b
		// not allowed to block!
		char *xx=NULL;*xx=0; }
	// scan the list, score is how many valid events from that docid
	unsigned long total = 0;
	for ( ; ! list.isExhausted() ; list.skipCurrentRec() ) {
		unsigned char *rec = (unsigned char *)list.getCurrentRec();
		// in datedb score is byte #5
		total += (255-rec[5]);
	}
	// declare
	char    *lastKeyPtr;
	key128_t  newStartKey;
	// add to count. datedb uses half keys so subtract 6 bytes
	// since the termids will be the same...
	//m_numEventsOnHost += list.getListSize() / (sizeof(key128_t)-6);
	m_numEventsOnHost += total;
	// bail if under limit
	if ( list.getListSize() < minRecSizes ) goto done;
	// update key
	lastKeyPtr = list.m_listEnd - 10;
	// we make a new start key
	list.getKey ( lastKeyPtr , (char *)&newStartKey );
	// maxxed out?
	if ( newStartKey.n0==0xffffffffffffffffLL &&
	     newStartKey.n1==0xffffffffffffffffLL )
		goto done;
	// sanity check
	if ( newStartKey < startKey ) { char *xx=NULL;*xx=0; }
	if ( newStartKey > endKey   ) { char *xx=NULL;*xx=0; }
	// inc it
	newStartKey.n0++;
	// in the top if the bottom wrapped
	if ( newStartKey.n0 == 0LL ) newStartKey.n1++;
	// assign
	startKey = newStartKey;
	// and loop back up for more now
	goto loop;

 done:

	// update all colls count
	g_collectiondb.m_numEventsAllColls += m_numEventsOnHost;

	if ( enabled ) g_threads.enableThreads();
	log("coll: got %li local events in termlist",m_numEventsOnHost);

	// set "m_hasDocQualityFiler"
	//updateFilters();

	return true;
}
*/

void CollectionRec::setUrlFiltersToDefaults ( ) {
	bool addDefault = false;
	if ( m_numRegExs == 0 ) 
		addDefault = true;
	//if ( m_numRegExs > 0 && strcmp(m_regExs[m_numRegExs-1],"default") )
	//	addDefault = true;
	if ( ! addDefault ) return;

	long n = 0;

	//strcpy(m_regExs   [n],"default");
	m_regExs[n].set("default");
	m_regExs[n].nullTerm();
	m_numRegExs++;

	m_spiderFreqs     [n] = 30; // 30 days default
	m_numRegExs2++;

	m_spiderPriorities[n] = 0;
	m_numRegExs3++;

	m_maxSpidersPerRule[n] = 99;
	m_numRegExs10++;

	m_spiderIpWaits[n] = 1000;
	m_numRegExs5++;

	m_spiderIpMaxSpiders[n] = 1;
	m_numRegExs6++;

	m_spidersEnabled[n] = 1;
	m_numRegExs7++;

	//m_spiderDiffbotApiNum[n] = 1;
	//m_numRegExs11++;
	m_spiderDiffbotApiUrl[n].set("");
	m_spiderDiffbotApiUrl[n].nullTerm();
	m_numRegExs11++;
}

/*
bool CrawlInfo::print (SafeBuf *sb ) {
	return sb->safePrintf("objectsAdded:%lli\n"
			      "objectsDeleted:%lli\n"
			      "urlsConsidered:%lli\n"
			      "downloadAttempts:%lli\n"
			      "downloadSuccesses:%lli\n"
			      "processAttempts:%lli\n"
			      "processSuccesses:%lli\n"
			      "lastupdate:%lu\n"
			      , m_objectsAdded
			      , m_objectsDeleted
			      , m_urlsConsidered
			      , m_pageDownloadAttempts
			      , m_pageDownloadSuccesses
			      , m_pageProcessAttempts
			      , m_pageProcessSuccesses
			      , m_lastUpdateTime
			      );
}

bool CrawlInfo::setFromSafeBuf (SafeBuf *sb ) {
	return sscanf(sb->getBufStart(),
		      "objectsAdded:%lli\n"
		      "objectsDeleted:%lli\n"
		      "urlsConsidered:%lli\n"
		      "downloadAttempts:%lli\n"
		      "downloadSuccesses:%lli\n"
		      "processAttempts:%lli\n"
		      "processSuccesses:%lli\n"
		      "lastupdate:%lu\n"
		      , &m_objectsAdded
		      , &m_objectsDeleted
		      , &m_urlsConsidered
		      , &m_pageDownloadAttempts
		      , &m_pageDownloadSuccesses
		      , &m_pageProcessAttempts
		      , &m_pageProcessSuccesses
		      , &m_lastUpdateTime
		      );
}
*/
	
// returns false on failure and sets g_errno, true otherwise
bool CollectionRec::save ( ) {
	if ( g_conf.m_readOnlyMode ) return true;
	//File f;
	char tmp[1024];
	//sprintf ( tmp , "%scollections/%li.%s/c.conf",
	//	  g_hostdb.m_dir,m_id,m_coll);
	// collection name HACK for backwards compatibility
	//if ( m_collLen == 0 )
	//	sprintf ( tmp , "%scoll.main/coll.conf", g_hostdb.m_dir);
	//else
	sprintf ( tmp , "%scoll.%s.%li/coll.conf", 
		  g_hostdb.m_dir , m_coll , (long)m_collnum );
	if ( ! g_parms.saveToXml ( (char *)this , tmp ) ) return false;
	// log msg
	//log (LOG_INFO,"db: Saved %s.",tmp);//f.getFilename());

	//
	// save the crawlinfo class in the collectionrec for diffbot
	//
	// SAVE LOCAL
	sprintf ( tmp , "%scoll.%s.%li/localcrawlinfo.dat",
		  g_hostdb.m_dir , m_coll , (long)m_collnum );
	//log("coll: saving %s",tmp);
	SafeBuf sb;
	//m_localCrawlInfo.print ( &sb );
	// binary now
	sb.safeMemcpy ( &m_localCrawlInfo , sizeof(CrawlInfo) );
	if ( sb.dumpToFile ( tmp ) == -1 ) {
		log("db: failed to save file %s : %s",
		    tmp,mstrerror(g_errno));
		g_errno = 0;
	}
	// SAVE GLOBAL
	sprintf ( tmp , "%scoll.%s.%li/globalcrawlinfo.dat",
		  g_hostdb.m_dir , m_coll , (long)m_collnum );
	//log("coll: saving %s",tmp);
	sb.reset();
	//m_globalCrawlInfo.print ( &sb );
	// binary now
	sb.safeMemcpy ( &m_globalCrawlInfo , sizeof(CrawlInfo) );
	if ( sb.dumpToFile ( tmp ) == -1 ) {
		log("db: failed to save file %s : %s",
		    tmp,mstrerror(g_errno));
		g_errno = 0;
	}
	
	// do not need a save now
	m_needsSave = false;
	return true;
}

// calls hasPermissin() below
bool CollectionRec::hasPermission ( HttpRequest *r , TcpSocket *s ) {
	long  plen;
	char *p     = r->getString ( "pwd" , &plen );
	long  ip    = s->m_ip;
	return hasPermission ( p , plen , ip );
}


// . does this password work for this collection?
bool CollectionRec::isAssassin ( long ip ) {
	// ok, make sure they came from an acceptable IP
	//for ( long i = 0 ; i < m_numSpamIps ; i++ ) 
	//	// they also have a matching IP, so they now have permission
	//	if ( m_spamIps[i] == ip ) return true;
	return false;
}

// . does this password work for this collection?
bool CollectionRec::hasPermission ( char *p, long plen , long ip ) {
	// just return true
	// collection permission is checked from Users::verifyColl 
	// in User::getUserType for every request
	return true;

	// scan the passwords
	// MDW: no longer, this is too vulnerable!!!
	/*
	for ( long i = 0 ; i < m_numAdminPwds ; i++ ) {
		long len = gbstrlen ( m_adminPwds[i] );
		if ( len != plen ) continue;
		if ( strncmp ( m_adminPwds[i] , p , plen ) != 0 ) continue;
		// otherwise it's a match!
		//goto checkIp;
		// . matching one password is good enough now, default OR
		// . because just matching an IP is good enough security,
		//   there is really no need for both IP AND passwd match
		return true;
	}
	*/
	// . if had passwords but the provided one didn't match, return false
	// . matching one password is good enough now, default OR
	//if ( m_numPasswords > 0 ) return false;
	// checkIp:
	// ok, make sure they came from an acceptable IP
	//for ( long i = 0 ; i < m_numAdminIps ; i++ ) 
	//	// they also have a matching IP, so they now have permission
	//	if ( m_adminIps[i] == ip ) return true;
	// if no security, allow all NONONONONONONONONO!!!!!!!!!!!!!!
	//if ( m_numAdminPwds == 0 && m_numAdminIps == 0 ) return true;
	// if they did not match an ip or password, even if both lists
	// are empty, do not allow access... this prevents security breeches
	// by accident
	return false;
	// if there were IPs then they failed to get in
	//if ( m_numAdminIps > 0 ) return false;
	// otherwise, they made it
	//return true;
}

// can this ip perform a search or add url on this collection?
bool CollectionRec::hasSearchPermission ( TcpSocket *s , long encapIp ) {
	// get the ip
	long ip = 0; if ( s ) ip = s->m_ip;
	// and the ip domain
	long ipd = 0; if ( s ) ipd = ipdom ( s->m_ip );
	// and top 2 bytes for the israel isp that has this huge block
	long ipt = 0; if ( s ) ipt = iptop ( s->m_ip );
	// is it in the ban list?
	/*
	for ( long i = 0 ; i < m_numBanIps ; i++ ) {
		if ( isIpTop ( m_banIps[i] ) ) {
			if ( m_banIps[i] == ipt ) return false;
			continue;
		}
		// check for ip domain match if this banned ip is an ip domain
		if ( isIpDom ( m_banIps[i] ) ) {
			if ( m_banIps[i] == ipd ) return false; 
			continue;
		}
		// otherwise it's just a single banned ip
		if ( m_banIps[i] == ip ) return false;
	}
	*/
	// check the encapsulate ip if any
	// 1091771468731 0 Aug 05 23:51:08 63.236.25.77 GET 
	// /search?code=mammaXbG&uip=65.87.190.39&n=15&raw=8&q=farm+insurance
	// +nj+state HTTP/1.0
	/*
	if ( encapIp ) {
		ipd = ipdom ( encapIp );
		ip  = encapIp;
		for ( long i = 0 ; i < m_numBanIps ; i++ ) {
			if ( isIpDom ( m_banIps[i] ) ) {
				if ( m_banIps[i] == ipd ) return false; 
				continue;
			}
			if ( isIpTop ( m_banIps[i] ) ) {
				if ( m_banIps[i] == ipt ) return false;
				continue;
			}
			if ( m_banIps[i] == ip ) return false;
		}
	}
	*/

	return true;
	/*
	// do we have an "only" list?
	if ( m_numSearchIps == 0 ) return true;
	// it must be in that list if we do
	for ( long i = 0 ; i < m_numSearchIps ; i++ ) {
		// check for ip domain match if this banned ip is an ip domain
		if ( isIpDom ( m_searchIps[i] ) ) {
			if ( m_searchIps[i] == ipd ) return true;
			continue;
		}
		// otherwise it's just a single ip
		if ( m_searchIps[i] == ip ) return true;
	}
	*/

	// otherwise no permission
	return false;
}

bool CollectionRec::rebuildUrlFilters ( ) {

	// only for diffbot custom crawls
	if ( m_isCustomCrawl != 1 && // crawl api
	     m_isCustomCrawl != 2 )  // bulk api
		return true;

	char *ucp = m_diffbotUrlCrawlPattern.getBufStart();
	if ( ucp && ! ucp[0] ) ucp = NULL;

	// if we had a regex, that works for this purpose as well
	if ( ! ucp ) ucp = m_diffbotUrlCrawlRegEx.getBufStart();
	if ( ucp && ! ucp[0] ) ucp = NULL;



	char *upp = m_diffbotUrlProcessPattern.getBufStart();
	if ( upp && ! upp[0] ) upp = NULL;

	// if we had a regex, that works for this purpose as well
	if ( ! upp ) upp = m_diffbotUrlProcessRegEx.getBufStart();
	if ( upp && ! upp[0] ) upp = NULL;


	// what diffbot url to use for processing
	char *api = m_diffbotApiUrl.getBufStart();
	if ( api && ! api[0] ) api = NULL;

	// convert from seconds to milliseconds. default is 250ms?
	long wait = (long)(m_collectiveCrawlDelay * 1000.0);
	// default to 250ms i guess. -1 means unset i think.
	if ( m_collectiveCrawlDelay < 0.0 ) wait = 250;

	// make the gigablast regex table just "default" so it does not
	// filtering, but accepts all urls. we will add code to pass the urls
	// through m_diffbotUrlCrawlPattern alternatively. if that itself
	// is empty, we will just restrict to the seed urls subdomain.
	for ( long i = 0 ; i < MAX_FILTERS ; i++ ) {
		m_regExs[i].purge();
		m_spiderPriorities[i] = 0;
		m_maxSpidersPerRule [i] = 10;
		m_spiderIpWaits     [i] = wait;
		m_spiderIpMaxSpiders[i] = 7; // keep it respectful
		m_spidersEnabled    [i] = 1;
		m_spiderFreqs       [i] =m_collectiveRespiderFrequency;
		m_spiderDiffbotApiUrl[i].purge();
		m_harvestLinks[i] = true;
	}

	long i = 0;


	// 1st default url filter
	m_regExs[i].set("ismedia && !ismanualadd");
	m_spiderPriorities   [i] = SPIDER_PRIORITY_FILTERED;
	i++;

	// 2nd default filter
	if ( m_restrictDomain ) {
		m_regExs[i].set("!isonsamedomain && !ismanualadd");
		m_spiderPriorities   [i] = SPIDER_PRIORITY_FILTERED;
		i++;
	}

	// 3rd rule for respidering
	if ( m_collectiveRespiderFrequency > 0.0 ) {
		m_regExs[i].set("lastspidertime>={roundstart}");
		// do not "remove" from index
		m_spiderPriorities   [i] = 10;
		// just turn off spidering. if we were to set priority to
		// filtered it would be removed from index!
		m_spidersEnabled     [i] = 0;
		i++;
	}
	// if collectiverespiderfreq is 0 or less then do not RE-spider
	// documents already indexed.
	else {
		// this does NOT work! error docs continuosly respider
		// because they are never indexed!!! like EDOCSIMPLIFIEDREDIR
		//m_regExs[i].set("isindexed");
		m_regExs[i].set("hasreply");
		m_spiderPriorities   [i] = 10;
		// just turn off spidering. if we were to set priority to
		// filtered it would be removed from index!
		m_spidersEnabled     [i] = 0;
		i++;
	}

	// and for docs that have errors respider once every 5 hours
	m_regExs[i].set("errorcount>0 && errcount<3");
	m_spiderPriorities   [i] = 40;
	m_spiderFreqs        [i] = 0.2; // half a day
	i++;

	// excessive errors? (tcp/dns timed out, etc.) retry once per month?
	m_regExs[i].set("errorcount>=3");
	m_spiderPriorities   [i] = 30;
	m_spiderFreqs        [i] = 30; // 30 days
	i++;

	// url crawl and process pattern
	if ( ucp && upp ) {
		m_regExs[i].set("matchesucp && matchesupp");
		m_spiderPriorities   [i] = 55;
		m_spiderDiffbotApiUrl[i].set ( api );
		i++;
		// if just matches ucp, just crawl it, do not process
		m_regExs[i].set("matchesucp");
		m_spiderPriorities   [i] = 54;
		i++;
		// just process, do not spider links if does not match ucp
		m_regExs[i].set("matchesupp");
		m_spiderPriorities   [i] = 53;
		m_harvestLinks       [i] = false;
		m_spiderDiffbotApiUrl[i].set ( api );
		i++;
		// do not crawl anything else
		m_regExs[i].set("default");
		m_spiderPriorities   [i] = SPIDER_PRIORITY_FILTERED;
		i++;
	}

	// harvest links if we should crawl it
	if ( ucp && ! upp ) {
		m_regExs[i].set("matchesucp");
		m_spiderPriorities   [i] = 54;
		// process everything since upp is empty
		m_spiderDiffbotApiUrl[i].set ( api );
		i++;
		// do not crawl anything else
		m_regExs[i].set("default");
		m_spiderPriorities   [i] = SPIDER_PRIORITY_FILTERED;
		i++;
	}

	// just process
	if ( upp && ! ucp ) {
		m_regExs[i].set("matchesupp");
		m_spiderPriorities   [i] = 53;
		//m_harvestLinks       [i] = false;
		m_spiderDiffbotApiUrl[i].set ( api );
		i++;
		// crawl everything by default, no processing
		m_regExs[i].set("default");
		m_spiderPriorities   [i] = 50;
		i++;
	}

	// no restraints
	if ( ! upp && ! ucp ) {
		// crawl everything by default, no processing
		m_regExs[i].set("default");
		m_spiderPriorities   [i] = 50;
		m_spiderDiffbotApiUrl[i].set ( api );
		i++;
	}

	m_numRegExs   = i;
	m_numRegExs2  = i;
	m_numRegExs3  = i;
	m_numRegExs10 = i;
	m_numRegExs5  = i;
	m_numRegExs6  = i;
	m_numRegExs7  = i;
	m_numRegExs8  = i;
	m_numRegExs11 = i;

	return true;
}
