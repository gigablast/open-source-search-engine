#include "gb-include.h"

#include "Collectiondb.h"
#include "CollectionRec.h"
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

// a global class extern'd in .h file
Collectiondb g_collectiondb;

Collectiondb::Collectiondb ( ) {
	m_numRecs = 0;
	m_numRecsUsed = 0;
	m_lastUpdateTime = 0LL;
}

// reset rdb
void Collectiondb::reset() {
	log("db: resetting collectiondb.");
	for ( long i = 0 ; i < m_numRecs ; i++ ) {
		if ( ! m_recs[i] ) continue;
		mdelete ( m_recs[i], sizeof(CollectionRec), "CollectionRec" ); 
		delete ( m_recs[i] );
		m_recs[i] = NULL;
	}
	m_numRecs     = 0;
	m_numRecsUsed = 0;
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
	log(LOG_INIT,"admin: Loading collection config files.");
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
		if ( !addRec ( coll , NULL , 0 , false , collnum , isDump ,
			       true ) )
			return false;
	}
	// note it
	log(LOG_INIT,"admin: Loaded data for %li collections. Ranging from "
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
	r->m_tree.cleanTree    ((char **)r->m_bases);
	//r = g_datedb.getRdb();
	//r->m_tree.cleanTree    ((char **)r->m_bases);

	r = g_titledb.getRdb();
	r->m_tree.cleanTree    ((char **)r->m_bases);
	//r = g_revdb.getRdb();
	//r->m_tree.cleanTree    ((char **)r->m_bases);
	//r = g_sectiondb.getRdb();
	//r->m_tree.cleanTree    ((char **)r->m_bases);
	//r = g_checksumdb.getRdb();
	//r->m_tree.cleanTree    ((char **)r->m_bases);
	//r = g_tfndb.getRdb();
	//r->m_tree.cleanTree    ((char **)r->m_bases);
	r = g_spiderdb.getRdb();
	r->m_tree.cleanTree    ((char **)r->m_bases);
	r = g_doledb.getRdb();
	r->m_tree.cleanTree    ((char **)r->m_bases);
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

// . MDW: TODO: bring this back when we have a subdir for each collection
// . add a new rec
// . returns false and sets g_errno on error
// . use a collnum_t of -1 if it is new
bool Collectiondb::addRec ( char *coll , char *cpc , long cpclen , bool isNew ,
			    collnum_t collnum , bool isDump ,
			    bool saveIt ) {
	// sanity check
	if ( ( isNew && collnum >= 0) ||
	     (!isNew && collnum <  0) ) {
		log(LOG_LOGIC,"admin: Bad parms passed to addRec.");
		char *xx = NULL; *xx = 0;
	}
	// ensure coll name is legit
	char *p = coll;
	for ( ; *p ; p++ ) {
		if ( is_alnum_a(*p) ) continue;
		if ( *p == '-' ) continue;
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
	long i ;
	if ( collnum >= 0 ) i = (long)collnum;
	else for ( i = 0 ; i < m_numRecs ; i++ ) if ( ! m_recs[i] ) break;
	// ceiling?
	if ( i >= MAX_COLLS ) {
		g_errno = ENOBUFS;
		return log("admin: Limit of %li collection reached. "
			   "Collection not created.",(long)MAX_COLLS);
	}
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
		return log("admin: Trying to create collection \"%s\" but "
			   "already exists in memory.",coll);
	}
	// MDW: ensure not created on disk since time of last load
	char dname[512];
	sprintf(dname, "%scoll.%s.%li/",g_hostdb.m_dir,coll,i);
	if ( isNew && opendir ( dname ) ) {
		g_errno = EEXIST;
		return log("admin: Trying to create collection %s but "
			   "directory %s already exists on disk.",coll,dname);
	}
	//char fname[512];
	// ending '/' is ALWAYS included in g_hostdb.m_dir
	//sprintf ( fname , "%s%li.%s.conf",g_hostdb.m_dir,i,coll);
	//File f;
	//f.set ( fname );
	//if ( f.doesExist() ) {
	//	g_errno = EEXIST;
	//	return log("admin: Trying to create collection \"%s\" but "
	//		   "file %s already exists on disk.",coll,fname);
	//}
	// create the record in memory
	m_recs[i] = new (CollectionRec);
	if ( ! m_recs[i] ) 
		return log("admin: Failed to allocated %li bytes for new "
			   "collection record for \"%s\".",
			   (long)sizeof(CollectionRec),coll);
	mnew ( m_recs[i] , sizeof(CollectionRec) , "CollectionRec" ); 
	// get copy collection
	CollectionRec *cpcrec = NULL;
	if ( cpc && cpc[0] ) cpcrec = getRec ( cpc , cpclen );
	if ( cpc && cpc[0] && ! cpcrec )
		log("admin: Collection \"%s\" to copy config from does not "
		    "exist.",cpc);
	// get the default.conf from working dir if there
	g_parms.setToDefault( (char *)m_recs[i] );

	if ( isNew ) {
		// the default conf file
		char tmp1[1024];
		sprintf ( tmp1 , "%sdefault.conf" , g_hostdb.m_dir );
		// . set our parms from the file.
		// . accepts OBJ_COLLECTIONREC or OBJ_CONF
		g_parms.setFromFile ( m_recs[i] , NULL , tmp1 );
	}

	// this will override all
	if ( cpcrec ) {
		// copy it, but not the timedb hashtable, etc.
		long size = (char *)&(cpcrec->m_END_COPY) - (char *)cpcrec;
		// JAB: bad memcpy - no donut!
		// this is not how objects are supposed to be copied!!!
		memcpy ( m_recs[i] , cpcrec , size);//sizeof(CollectionRec) );
		// perform the cleanup that a copy constructor might do...
		//for (int rx = 0; rx < MAX_FILTERS; rx++)
		//	m_recs[i]->m_pRegExParser[rx] = NULL;
		// don't NUKE the filters!
		// m_recs[i]->m_numRegExs = 0;
		// OK - done with cleaning up...
		// but never copy over the collection hostname, that is
		// problematic
		m_recs[i]->m_collectionHostname [0] = '\0';
		m_recs[i]->m_collectionHostname1[0] = '\0';
		m_recs[i]->m_collectionHostname2[0] = '\0';
	}

	// set coll id and coll name for coll id #i
	strcpy ( m_recs[i]->m_coll , coll );
	m_recs[i]->m_collLen = gbstrlen ( coll );
	m_recs[i]->m_collnum = i;

	// point to this, so Rdb and RdbBase can reference it
	coll = m_recs[i]->m_coll;

	// . if has no password or ip add the default password, footbar
	// . no, just don't have any password, just use the 127.0.0.1 ip
	//   that is the loopback
	/*
	if ( m_recs[i]->m_numAdminIps  == 0 &&
	     m_recs[i]->m_numAdminPwds == 0    ) {
		m_recs[i]->m_numAdminIps = 1;
		m_recs[i]->m_adminIps[0] = atoip("0.0.0.0",7);
		//strcpy ( m_recs[i]->m_adminPwds[0] , "footbar23" );
		//m_recs[i]->m_numAdminPwds = 1;
		//log("admin: Using default password for new collection of "
		//    "'footbar23'.");
	}
	*/

	// collection name HACK for backwards compatibility
	//if ( strcmp ( coll , "main" ) == 0 ) {
	//	m_recs[i]->m_coll[0] = '\0';
	//	m_recs[i]->m_collLen = 0;
	//	//coll[0] = '\0';
	//}

	// MDW: create the new directory
	if ( isNew ) {
	retry22:
		if ( ::mkdir ( dname , 
			       S_IRUSR | S_IWUSR | S_IXUSR | 
			       S_IRGRP | S_IWGRP | S_IXGRP | 
			       S_IROTH | S_IXOTH ) ) {
			// valgrind?
			if ( errno == EINTR ) goto retry22;
			g_errno = errno;
			mdelete ( m_recs[i] , sizeof(CollectionRec) , 
				  "CollectionRec" ); 
			delete ( m_recs[i]);
			m_recs[i] = NULL;
			return log("admin: Creating directory %s had error: "
				   "%s.", dname,mstrerror(g_errno));
		}
		// save it into this dir... might fail!
		if ( ! m_recs[i]->save() ) {
			mdelete ( m_recs[i] , sizeof(CollectionRec) , 
				  "CollectionRec" ); 
			delete ( m_recs[i]);
			m_recs[i] = NULL;
			return log("admin: Failed to save file %s: %s",
				   dname,mstrerror(g_errno));
		}
	}
	// load if not new
	if ( ! isNew && ! m_recs[i]->load ( coll , i ) ) {
		mdelete ( m_recs[i], sizeof(CollectionRec), "CollectionRec" ); 
		delete ( m_recs[i]);
		m_recs[i] = NULL;
		return log("admin: Failed to load conf for collection "
			   "\"%s\".",coll);
	}
	// mark it as needing to be saved instead
	m_recs[i]->m_needsSave = false;
	// force this to off for now
	//m_recs[i]->m_queryExpansion = false;
	// reserve it
	if ( i >= m_numRecs ) m_numRecs = i + 1;
	// count it
	m_numRecsUsed++;
	// update the time
	updateTime();
	// if we are doing a dump from the command line, skip this stuff
	if ( isDump ) return true;
	bool verify = true;
	if(isNew) verify = false;
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
	if ( ! g_spiderdb.addColl   ( coll, verify ) ) goto hadError;
	if ( ! g_doledb.addColl     ( coll, verify ) ) goto hadError;
	//if ( ! g_tfndb.addColl      ( coll, verify ) ) goto hadError;
	if ( ! g_clusterdb.addColl  ( coll, verify ) ) goto hadError;
	if ( ! g_linkdb.addColl     ( coll, verify ) ) goto hadError;
	// debug message
	log ( LOG_INFO, "admin: added collection \"%s\" (%li).",coll,(long)i);
	// tell SpiderCache about this collection, it will create a 
	// SpiderCollection class for it.
	//g_spiderCache.reset1();

	// . make it set is CollectionRec::m_sortByDateTable now
	// . everyone else uses setTimeOfDayInMilliseconds() in fctypes.cpp
	//   to call this function once their clock is synced with host #0
	//if ( g_hostdb.m_initialized && g_hostdb.m_hostId == 0 )
	//	initSortByDateTable(coll);
	//else if ( g_hostdb.m_initialized && isClockInSync() )
	//	initSortByDateTable(coll);
	// . do it for all regard-less
	// . once clock is in sync with host #0 we may do it again!
	//if ( g_hostdb.m_initialized )
	//	initSortByDateTable(coll);

	// success
	return true;
 hadError:
	log("admin: Had error adding new collection: %s.",mstrerror(g_errno));
	// do not delete it, might have failed to add because not enough
	// memory to read in the tree *-saved.dat file on disk!! and if
	// you delete in then core the *-saved.dat file gets overwritten!!!
	return false;
	/*
	g_indexdb.getRdb()->delColl    ( coll );
	g_datedb.getRdb()->delColl     ( coll );
	g_timedb.getRdb()->delColl     ( coll );
	g_titledb.getRdb()->delColl    ( coll );
	g_revdb.getRdb()->delColl      ( coll );
	g_sectiondb.getRdb()->delColl  ( coll );
	g_placedb.getRdb()->delColl    ( coll );
	g_tagdb.getRdb()->delColl      ( coll );
	//g_catdb.getRdb()->delColl      ( coll );
	//g_checksumdb.getRdb()->delColl ( coll );
	g_spiderdb.getRdb()->delColl   ( coll );
	g_doledb.getRdb()->delColl     ( coll );
	g_tfndb.getRdb()->delColl      ( coll );
	g_clusterdb.getRdb()->delColl  ( coll );
	g_linkdb.getRdb()->delColl     ( coll );
	deleteRec                      ( coll );
	return false;
	*/
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

// delete all records checked in the list
bool Collectiondb::deleteRecs ( HttpRequest *r ) {
	for ( long i = 0 ; i < r->getNumFields() ; i++ ) {
		char *f = r->getField ( i );
		if ( strncmp ( f , "del" , 3 ) != 0 ) continue;
		char *coll = f + 3;
		//if ( ! is_digit ( f[3] )            ) continue;
		//long h = atol ( f + 3 );
		deleteRec ( coll );
	}
	return true;
}

// . delete a collection
// . this uses blocking unlinks, may make non-blocking later
bool Collectiondb::deleteRec ( char *coll , bool deleteTurkdb ) {
	// force on for now
	deleteTurkdb = true;
	// no spiders can be out. they may be referencing the CollectionRec
	// in XmlDoc.cpp... quite likely.
	if ( g_conf.m_spideringEnabled ||
	     g_spiderLoop.m_numSpidersOut > 0 ) {
		log("admin: Can not delete collection while "
		    "spiders are enabled or active.");
		return false;
	}
	// do not allow this if in repair mode
	if ( g_repairMode > 0 ) {
		log("admin: Can not delete collection while in repair mode.");
		return false;
	}
	// ensure it's not NULL
	if ( ! coll ) {
		log(LOG_LOGIC,"admin: Collection name to delete is NULL.");
		return false;
	}
	// find the rec for this collection
	collnum_t collnum = getCollnum ( coll );
	// bitch if not found
	if ( collnum < 0 ) {
		g_errno = ENOTFOUND;
		return log(LOG_LOGIC,"admin: Collection \"%s\" not found, "
			   "delete failed.",coll);
	}
	CollectionRec *cr = m_recs [ collnum ];
	if ( ! cr ) return log("admin: Collection id problem. Delete failed.");
	// we need a save
	m_needsSave = true;
	// nuke it on disk
	char oldname[1024];
	sprintf(oldname, "%scoll.%s.%li/",g_hostdb.m_dir,cr->m_coll,
		(long)cr->m_collnum);
	char newname[1024];
	sprintf(newname, "%strash/coll.%s.%li.%lli/",g_hostdb.m_dir,cr->m_coll,
		(long)cr->m_collnum,gettimeofdayInMilliseconds());
	//Dir d; d.set ( dname );
	// ensure ./trash dir is there
	char trash[1024];
	sprintf(trash, "%strash/",g_hostdb.m_dir);
	::mkdir ( trash, 
		  S_IRUSR | S_IWUSR | S_IXUSR | 
		  S_IRGRP | S_IWGRP | S_IXGRP | 
		  S_IROTH | S_IXOTH ) ;
	// move into that dir
	::rename ( oldname , newname );
	// debug message
	logf ( LOG_INFO, "admin: deleted collection \"%s\" (%li).",
	       coll,(long)collnum );

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
	if ( deleteTurkdb ) {
	}
	//g_catdb.getRdb()->delColl      ( coll );
	//g_checksumdb.getRdb()->delColl ( coll );
	g_spiderdb.getRdb()->delColl   ( coll );
	g_doledb.getRdb()->delColl     ( coll );
	//g_tfndb.getRdb()->delColl      ( coll );
	g_clusterdb.getRdb()->delColl  ( coll );
	g_linkdb.getRdb()->delColl     ( coll );
	// free it
	mdelete ( m_recs[(long)collnum], sizeof(CollectionRec), 
		  "CollectionRec" ); 
	delete ( m_recs[(long)collnum] );
	m_recs[(long)collnum] = NULL;
	// dec counts
	m_numRecsUsed--;
	while ( ! m_recs[m_numRecs-1] ) m_numRecs--;
	// update the time
	updateTime();
	// done
	return true;
}

#include "PageTurk.h"

// . reset a collection
// . returns false if failed
bool Collectiondb::resetColl ( char *coll , bool resetTurkdb ) {
	// ensure it's not NULL
	if ( ! coll ) {
		log(LOG_LOGIC,"admin: Collection name to delete is NULL.");
		return false;
	}
	// now must be "test" only for now
	if ( strcmp(coll,"test") ) { char *xx=NULL;*xx=0; }
	// no spiders can be out. they may be referencing the CollectionRec
	// in XmlDoc.cpp... quite likely.
	if ( g_conf.m_spideringEnabled ||
	     g_spiderLoop.m_numSpidersOut > 0 ) {
		log("admin: Can not delete collection while "
		    "spiders are enabled or active.");
		return false;
	}
	// do not allow this if in repair mode
	if ( g_repairMode > 0 ) {
		log("admin: Can not delete collection while in repair mode.");
		return false;
	}
	// get the CollectionRec for "test"
	CollectionRec *cr = getRec ( "test" );
	// must be there. if not, we create test i guess
	if ( ! cr ) { 
		log("db: could not get test coll rec");
		char *xx=NULL;*xx=0; 
	}

	// make sure an update not in progress
	if ( cr->m_inProgress ) { char *xx=NULL;*xx=0; }

	CollectionRec tmp; 

	// copy it to "tmp"
	long size = (char *)&(cr->m_END_COPY) - (char *)cr;
	// do not copy the hashtable crap since you will have to re-init it!
	memcpy ( &tmp , cr , size ); // sizeof(CollectionRec) );

	// delete the test coll now
	if ( ! deleteRec ( "test" , resetTurkdb  ) ) 
		return log("admin: reset coll failed");

	// make a collection called "test2" so that we copy "test"'s parms
	bool status = addRec ( "test" , 
			       NULL , 
			       0 ,
			       true , // bool isNew ,
			       (collnum_t) -1 ,
			       // not a dump
			       false ,
			       // do not save it!
			       false );
	// bail on error
	if ( ! status ) return log("admin: failed to add new coll for reset");
	// get its rec
	CollectionRec *nr = getRec ( "test" );
	// must be there
	if ( ! nr ) { char *xx=NULL;*xx=0; }
	// save this though, this might have changed!
	collnum_t cn = nr->m_collnum;
	// overwrite its rec
	memcpy ( nr , &tmp , size ) ; // sizeof(CollectionRec) );
	// put that collnum back
	nr->m_collnum = cn;
	// set the flag
	m_needsSave = true;
	// save it again after copy
	nr->save();

	// and clear the robots.txt cache in case we recently spidered a
	// robots.txt, we don't want to use it, we want to use the one we
	// have in the test-parser subdir so we are consistent
	RdbCache *robots = Msg13::getHttpCacheRobots();
	RdbCache *others = Msg13::getHttpCacheOthers();
	robots->clear ( cn );
	others->clear ( cn );

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
	if ( ! coll ) coll = "";
	for ( long i = 0 ; i < m_numRecs ; i++ ) {
		if ( ! m_recs[i] ) continue;
		if ( m_recs[i]->m_coll[0] != coll[0] ) continue;
		if ( strcmp ( m_recs[i]->m_coll , coll ) == 0 ) return i;
	}
	// This is necessary for Statsdb to work, as it is
	// not associated with any collection. Is this
	// necessary for Catdb?
	if ( strcmp ( "statsdb\0", coll ) == 0) return 0;
	if ( strcmp ( "facebookdb\0", coll ) == 0) return 0;
	if ( strcmp ( "accessdb\0", coll ) == 0) return 0;
	//if ( strcmp ( "catdb\0", coll ) == 0) return 0;
	return (collnum_t)-1; // not found
}

collnum_t Collectiondb::getCollnum ( char *coll , long clen ) {
	if ( ! coll ) coll = "";
	for ( long i = 0 ; i < m_numRecs ; i++ ) {
		if ( ! m_recs[i] ) continue;
		if ( m_recs[i]->m_collLen != clen ) continue;
		if ( strncmp(m_recs[i]->m_coll,coll,clen) == 0 ) return i;
	}
	// This is necessary for Statsdb to work, as it is
	// not associated with any collection. Is this
	// necessary for Catdb?
	if ( strncmp ( "statsdb\0", coll, clen ) == 0) return 0;
	if ( strcmp ( "facebookdb\0", coll ) == 0) return 0;
	if ( strncmp ( "accessdb\0", coll, clen ) == 0) return 0;
	//if ( strncmp ( "catdb\0", coll, clen ) == 0) return 0;
	return (collnum_t)-1; // not found
}

collnum_t Collectiondb::getNextCollnum ( collnum_t collnum ) {
	for ( long i = (long)collnum + 1 ; i < m_numRecs ; i++ ) 
		if ( m_recs[i] ) return i;
	// no next one, use -1
	return (collnum_t) -1;
}

