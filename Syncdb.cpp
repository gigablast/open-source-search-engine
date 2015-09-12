#include "gb-include.h"

#include "Syncdb.h"
#include "Threads.h"
#include "Msg4.h"
#include "Process.h"

// a global class extern'd in .h file
Syncdb g_syncdb;

static void gotListWrapper ( void *state , RdbList *list , Msg5 *msg5 ) ;
static void gotListWrapper4 ( void *state ) ;
static void addedListWrapper5 ( void *state , UdpSlot *slot ) ;

static void handleRequest5d ( UdpSlot *slot , int32_t netnice ) ;
static void handleRequest55 ( UdpSlot *slot , int32_t netnice ) ;

static void gotReplyWrapper55 ( void *state , UdpSlot *slot ) ;
static void syncDoneWrapper ( void *state , ThreadEntry *te ) ;
static void *syncStartWrapper_r ( void *state , ThreadEntry *te ) ;

// returns false with g_errno set on error
bool Syncdb::gotMetaListRequest ( UdpSlot *slot ) {

	// get the request buffer
	char *req     = slot->m_readBuf;
	int32_t  reqSize = slot->m_readBufSize;
	// get sender hostid
	uint32_t sid = slot->m_hostId;

	return gotMetaListRequest ( req, reqSize , sid );
}

bool Syncdb::gotMetaListRequest ( char *req , int32_t reqSize , uint32_t sid ) {
	// use 0 for this
	uint32_t tid = 0;
	// point to it
	uint64_t zid = *(uint64_t *)(req + 4);
	// sanity check
	if ( sid < 0 ) { char *xx=NULL;*xx=0; }
	// get the key of the request. use 0 for tid.
	key128_t k = makeKey ( 0,0,1,0,tid,sid,zid,1);

	logf(LOG_DEBUG,"syncdb: got meta list request");

	// if tree is too full, deny it!
	if ( m_qt.is90PercentFull() ) {
		g_errno = ETRYAGAIN; return false; }

	// . see if there is a msg4 we already added after it from same sid
	// . there may have been if we just woke up from a slumber...
	// . so allow out of order meta lists to be added still
	/*
	key128_t ak = makeKey (0,0,1,0,tid,sid,zid);
	key128_t ek = makeKey (0,0,1,0,tid,sid,0xffffffffffffffff);
	int32_t nn = m_qt.getNextNode ( &ak );
	// get key if any
	if ( nn >= 0 ) {
		key128_t ck  = *(key128_t *)m_qt.getKey ( nn );
		// if in range, that is strange
		if ( k <= ck ) {
			log("sync: got msg4 meta list with expired key: "
			    "k .n1=0x%"XINT64" k .n0=0x%"XINT64" "
			    "ck.n1=0x%"XINT64" ck.n0=0x%"XINT64" " ,
			    k.n1,k.n0,ck.n1,ck.n0 );
			// pretend we added it
			return true;
		}
	}
	*/

	// . add the record (which contains a list)
	// . it starts with the syncdb key
	// . see below for the defition of the bits in the key
	// . might set g_errno to ETRYAGAIN
	if ( ! m_rdb.addRecord ((collnum_t)0,(char *)&k , req , reqSize , 
				MAX_NICENESS ) )
		return false;

	// . ADD THE C KEY
	// . add key with "c" set to 1 to indicate we need to add the meta list
	// . use "0" for tid since does not apply here
	// . if add did not work, return false and set g_errno
	// . i think g_errno is usually ETRYAGAIN
	// . it should initiate a dump of the tree to make room
	// . Msg4 handlerRequest() should send back ETRYAGAIN
	key128_t k2 = makeKey (0,0,1,0,tid,sid,zid,1);
	if ( m_qt.addKey ( &k2 ) < 0 ) { char *xx=NULL;*xx=0; }

	// . ADD THE D KEY
	// . and a key with the "d" set to indicate we need to delete the meta
	//   list (after we got all requests, replies and added it ourselves)
	key128_t k3 = makeKey (0,0,0,1,tid,sid,zid,1);
	if ( m_qt.addKey ( &k3 ) < 0 ) { char *xx=NULL;*xx=0; }

	// . add the individiual checkoff keys
	// . 1 key per hostid in our mirror group
	// . we delete these keys using msg1 on successful transmissions
	int32_t  nh    = g_hostdb.getNumHostsPerShard();
	Host *hosts = g_hostdb.getMyShard();
	for ( int32_t i = 0 ; i < nh ; i++ ) {
		// get it
		Host *h = &hosts[i];
		// skip if us
		if ( h == g_hostdb.m_myHost ) continue;
		// these actually have a twin id (tid)
		int32_t tid2 = h->m_hostId;
		// set "a" to 1 to indicate we need to send a checkoff request
		// set "b" to 1 to indicate we need to recv a checkoff request
		key128_t k4 = makeKey ( 1,0,0,0,tid2,sid,zid,1 );
		key128_t k5 = makeKey ( 0,1,0,0,tid2,sid,zid,1 );

		// . ADD THE A KEY
		// . if add did not work, return false and set g_errno
		// . i think g_errno is usually ETRYAGAIN
		// . it should initiate a dump of the tree to make room
		// . Msg4 handlerRequest() should send back ETRYAGAIN
		if ( m_qt.addKey ( &k4 ) < 0 ) { char *xx=NULL;*xx=0; }

		// . if we already did recv the check off request, delete it!
		key128_t ck = makeKey ( 0,1,0,0,tid2,sid,zid,0 );
		int32_t     dn = m_qt.getNode ( 0, (char *)&ck ) ;
		// hey, we annihilated with the positive key. i guess our
		// twin is ahead of us!
		if ( dn >= 0 ) m_qt.deleteNode3 ( dn , false );
		// . ADD the B KEY
		// . we did not annihilate with a negative key
		else if ( m_qt.addKey ( &k5 ) < 0 ) { char *xx=NULL;*xx=0; }
	}

	logf(LOG_DEBUG,"syncdb: added a b c and d keys to quick tree "
	     "tid=%"UINT32" sid=%"UINT32" zid=%"UINT64"",(int32_t)tid,(int32_t)sid,(int64_t)zid);


	// success
	return true;
}


// . QUICKTREE KEY =
//
// . abcd0000 iiiiiiii iiiiiiii iiiiiiii  i = twinHostId
// . 00000000 ssssssss ssssssss ssssssss  s = original msg4 sender hostid
// . zzzzzzzz zzzzzzzz zzzzzzzz zzzzzzzz  z = zid
// . zzzzzzzz zzzzzzzz zzzzzzzz zzzzzzzD
// .
// . "a" is 1 to indicate we need to send a checkoff request to   host "i"
// . "b" is 1 to indicate we need to recv a checkoff request from host "i"
// . "c" is 1 to indicate we need to add the meta list to our rdbs
// . "d" is 1 to indicate we need to delete the meta list from syncdb
// . "D" is 0 to indicate a delete key (negative key) (task completed)
// . "s" = "sid", hostid that sent the msg request originally = "sender id"
// . "z" = "zid", the sender's "transaction id" for that msg4. always increases
// . "i" = "tid", hostid of the twin in our group that we are chatting with
//                to make sure he adds this msg4 add request reliably
key128_t Syncdb::makeKey ( char     a      ,
			   char     b      ,
			   char     c      ,
			   char     d      ,
			   uint32_t tid    ,
			   uint32_t sid    ,
			   uint64_t zid    ,
			   char     delBit ) { 
	key128_t k;
	uint64_t n1 = 0;
	if ( a ) n1 |= 0x01;
	n1 <<= 1;
	if ( b ) n1 |= 0x01;
	n1 <<= 1;
	if ( c ) n1 |= 0x01;
	n1 <<= 1;
	if ( d ) n1 |= 0x01;
	n1 <<= (4+24);
	n1 |= (tid & 0x00ffffff);
	// make room for sid
	n1 <<= 32;
	n1 |= (sid & 0x00ffffff);
	// set it
	k.n1 = n1;
	// sanity check, low bit must be reserved for del bit
	if ( (zid & 0x01) && ! delBit ) { char *xx=NULL;*xx=0; }
	// lowest 63 bits of zid
	k.n0 = zid ;
	// and del bit
	if ( delBit ) k.n0 |= 0x01;
	return k;
}

// . ***** META LIST PRE-ADD LOOP *****
// . scan for meta lists to add
// . scan m_qt (quickTree) for keys with the "c" bit set
// . the "c" bit means we need to add the meta list for that sid/zid
// . add meta list keys to the m_addme[] array so loop2() can loop over syncdb
//   itself and add the meta lists corresponding to the keys in m_addme[]
//   since the meta list may be on disk cuz they are very big (32k+)
// . call this once per second or so, or back-to-back if we "did something"
// . this loop will set the m_addme[] array to keys in syncdb of metalists
//   that we can finally add!
void Syncdb::loop1 ( ) {
	// do not re-call this loop this round in bigLoop() function
	m_calledLoop1 = true;
	// how many hosts in our group are alive?
	int32_t alive = 0;
	// get group we are in
	Host *group = g_hostdb.getMyShard();
	// number hosts in group
	int32_t nh = g_hostdb.getNumHostsPerShard();
	// count alive
	for ( int32_t i = 0 ; i < nh ; i++ )
		if ( ! g_hostdb.isDead ( &group[i] ) ) alive++;
	// reset
	m_na = 0;
	m_ia = 0;
	// . if we do NOT have 2+ alive hosts, including ourselves, bail
	// . at least two twins a group must be alive to do an add otherwise 
	//   we risk serious data loss, since hard drives tend to die mostly 
	//   when writing!
	if ( alive <= 1 ) return;

	// . loop over the meta lists we need to add
	// . use a "tid" of 0
	int32_t     tid = 0;
	key128_t sk  = makeKey(0,0,1,0,tid,0,0,0);
	key128_t ek  = makeKey(0,0,1,0,tid,0xffffffff,0xffffffffffffffffLL,1);

	// get the first node in sequence, if any. 0 = collnum
	int32_t nn = m_qt.getNextNode ( 0 , (char *)&sk );
	// do the loop
	for ( ; nn >= 0 ; nn = m_qt.getNextNode ( nn ) ) {
		// breathe
		QUICKPOLL ( MAX_NICENESS );
		// get key
		key128_t k = *(key128_t *)m_qt.getKey ( nn );
		// stop when we hit the end
		if ( k > ek ) break;
		// get zid
		uint64_t zid = getZid ( &k );
		// get sid
		uint32_t sid = getSid ( &k );
		// . if we have a "need to send checkoff request" key still
		//   present in quicktree, that means we never received a good
		//   reply for it! and we reqire that! 
		// . if a host is dead and we need to send a checkoff request
		//   to him, then we do not count that here...
		if ( ! sentAllCheckoffRequests ( sid , zid ) ) {
			// no use banging away at this sid any more since we
			// are missing a checkoff reply from an alive twin
			sid++;
			// find the key of the FIRST meta list we need to add
			// for this new senderId, "sid"
			key128_t nk = makeKey ( 0,0,1,0,tid,sid,0,0 );
			// undo the m_qt.getNextNode(nn) we call in for loop
			nn = m_qt.getPrevNode ( 0 , (char *)&nk );
			// sanity check
			if ( nn < 0 ) { char *xx=NULL; *xx=0; }
			// get next key from this new sid
			continue;
		}
		// schedule it for an add in loop2() below
		m_addMe [ m_na++ ] = k;
		// crap no room! wait for it to be used up!
		if ( m_na >= MAX_TO_ADD ) return;
	}
}
// . have we sent all the checkoff requests (and got replies) for this sid/zid?
// . we cannot add a meta list until we have
bool Syncdb::sentAllCheckoffRequests ( uint32_t sid , uint64_t zid ) {
	// get group we are in
	Host *group = g_hostdb.getMyShard();
	// number hosts in group
	int32_t nh = g_hostdb.getNumHostsPerShard();
	// loop over our twins
	for ( int32_t i = 0 ; i < nh ; i++ ) {
		// get host
		Host *h = &group[i];
		// skip if us
		if ( h == g_hostdb.m_myHost ) continue;
		// skip if dead
		if ( g_hostdb.isDead ( h ) ) continue;
		// make the key
		key128_t k = makeKey(1,0,0,0,h->m_hostId,sid,zid,1);
		// get it
		int32_t nn = m_qt.getNode ( 0 , (char *)&k );
		// if it is there, that means we have yet to get a reply
		// for this checkoff request! because once we get a successful
		// reply for it, we delete it from quick tree
		if ( nn >= 0 ) return false;
	}
	// i guess we are good to go!
	return true;
}



// . ***** META LIST ADD LOOP *****
// . add meta lists corresponding to the keys in m_addme[]
// . call msg5 in case some got dumped to disk!
// . never add zids out of order for the same sid
// . returns false if blocked, sets g_errno on error and returns true
bool Syncdb::loop2 ( ) {
	// sanity check
	if ( ! m_calledLoop1 ) { char *xx=NULL;*xx=0; }
 loop:
	// breathe just in case
	QUICKPOLL ( MAX_NICENESS );
	// return if done!
	if ( m_ia >= m_na ) { m_calledLoop2 = true ; return true; }
	// . check the tree for the next key to add first!!!
	// . most of the time this should be the case!
	RdbTree *stree = &m_rdb.m_tree;
	// get the key
	key128_t k = m_addMe[m_ia];
	// is it there?
	int32_t n = stree->getNode ( 0 , (char *)&k );
	// yes! easy add...
	if ( n >= 0 ) {
		//int32_t  reqSize = stree->getDataSize ( n );
		char   *req     = stree->getData     ( n );
		// get zid from key
		uint64_t zid1 = getZid ( &k );
		// get zid from request
		uint64_t zid2 = *(uint64_t *)(req+4);
		// must match!
		if ( zid1 != zid2 ) { char *xx=NULL;*xx=0; }
		// . add away using Msg4.cpp
		// . return with g_errno set on error
		if ( ! addMetaList ( req ) ) return true;
		// success, delete the key from QUICK TREE
		m_qt.deleteNode ( 0 , (char *)&k , true );
		// . and delete that node (freeData = true)
		// . no! not until we got all the checkoff requests in!
		// stree->deleteNode3 ( n , true );
		// advance on success
		m_ia++;
		// go back for more
		goto loop;
	}
	// make the key range
	key128_t sk = k;
	key128_t ek = k;
	// make negative
	sk.n0 &= 0xfffffffffffffffeLL;
	// do not let sleep ticker call bigLoop
	m_outstanding = true;
	// get the meta list from on disk i guess, if not in tree
	if ( ! m_msg5.getList ( RDB_SYNCDB     ,
				(collnum_t)0           , // coll
				&m_list        ,
				(char *)&sk    , // startKey
				(char *)&ek    , // endKey
				999            , // minRecSizes
				true           , // includeTree?
				false          , // addToCache
				0              , // maxCacheAge
				0              , // startFielNum
				-1             , // numFiles
				NULL           , // state
				gotListWrapper ,
				MAX_NICENESS   ,
				true           )) // do error correction?
		return false;
	// process it. loop back up for another on success!
	if ( gotList ( ) ) goto loop;
	// bad. g_errno must be set
	return false;
}
void gotListWrapper ( void *state , RdbList *list , Msg5 *msg5 ) {
	// do not re-loop on error
	if ( ! g_syncdb.gotList ( ) ) return;
	// loop backup!
	g_syncdb.loop2();
}
// . send as many zids with a msg4d request as you can
// . that way we check a bunch off all at once
bool Syncdb::gotList ( ) {
	// bigLoop() can be called again now
	m_outstanding = false;
	// error?
	if ( g_errno ) {
		log("sync: had error in msg5: %s",mstrerror(g_errno));
		return false;
	}
	// int16_tcut
	RdbList *m = &m_list;
	// just in case
	m->resetListPtr();
	// get the rec
	char *rec = m->getCurrentRec();
	// get key
	key128_t k = *(key128_t *)rec;
	// sanity check
	if ( k != m_addMe[m_ia] ) { char *xx=NULL;*xx=0;}
	// . add it using msg4.cpp::addMetaList()
	// . sets g_errno and returns false on error
	if ( ! addMetaList ( rec ) ) return false;
	// we no longer have to add it!
	m_qt.deleteNode ( 0 , (char *)&k , true );
	// point to next
	m_ia++;
	// free it
	m_list.reset();
	// success
	return true;
}


// . ***** META LIST DELETE LOOP *****
// . scan for meta lists to remove from syncdb
// . check every D KEY
// . must NOT have any "need to send request" keys (a bit set)
// . must NOT have any "need to recv request" keys (b bit set)
// . must NOT have our "need to add" key (c bit set)
void Syncdb::loop3 ( ) {
	// . loop over the meta lists we need to delete
	// . these are "d" keys
	// . use a "tid" of 0
	key128_t sk = makeKey ( 0,0,0,1,0,0,0,0 );
	key128_t ek = makeKey ( 0,0,0,1,0,0xffffffff,0xffffffffffffffffLL,1 );
	// get the first node in sequence, if any
	int32_t nn = m_qt.getNextNode ( 0 , (char *)&sk );
	// do the loop
	for ( ; nn >= 0 ; nn = m_qt.getNextNode ( nn ) ) {
		// breathe
		QUICKPOLL ( MAX_NICENESS );
		// get key
		key128_t k = *(key128_t *)m_qt.getKey ( nn );
		// stop when we hit the end
		if ( k > ek ) break;
		// get zid
		uint64_t zid = getZid ( &k );
		// get sid
		uint32_t sid = getSid ( &k );
		// have we sent/recvd all checkoff requests required? have
		// we added the meta list? if so, we can nuke it from syncdb
		if ( ! canDeleteMetaList ( sid, zid ) ) {
			// no use banging away at this sid any more since we
			// are missing another action for this one
			sid++;
			// find the key of the FIRST meta list we need to add
			// for this new senderId, "sid"
			key128_t nk = makeKey ( 0,0,0,1,0,sid,0,0 );
			// undo the m_qt.getNextNode(nn) we call in for loop
			nn = m_qt.getPrevNode ( 0 , (char *)&nk );
			// sanity check
			if ( nn < 0 ) { char *xx=NULL;*xx=0; }
			// get next key from this new sid
			continue;
		}
		// . make the negative key for syncdb
		// . it just uses a negative "c" key, with a tid of 0
		key128_t dk = makeKey ( 0,0,1,0,0,sid,zid,0);
		// . add it to syncdb to signifiy a delete
		// . this returns false and sets g_errno on error
		if(!m_rdb.addRecord((collnum_t)0,(char *)&dk,NULL,0,
				    MAX_NICENESS)) return;
		// delete it from quick tree now that we added the negative
		// key successfully to syncdb
		int32_t dn = m_qt.getNode ( 0, (char *)&k );
		// must be there!
		if ( ! dn ) { char *xx=NULL;*xx=0; }
		// nuke it
		m_qt.deleteNode3 ( dn , true );
	}
	// . success
	// . do not recall until big loop completes a round
	m_calledLoop3 = true;
}
// . have we added the meta list to our rdbs yet?
// . can not delete the actual meta list from syncdb until so
bool Syncdb::canDeleteMetaList ( uint32_t sid , uint64_t zid ) {
	// make the "c" key to see if we still need to add the meta list
	key128_t k = makeKey(0,0,1,0,0,sid,zid,1);
	// get it
	int32_t nn = m_qt.getNode ( 0 , (char *)&k );
	// if "c" key is there, that means we need to add to our rdb still
	// so we can not delete it yet!
	if ( nn >= 0 ) return false;
	// get group we are in
	Host *group = g_hostdb.getMyShard();
	// . if we need to send some requests still can not delete
	// . number hosts in group
	int32_t nh = g_hostdb.getNumHostsPerShard();
	// loop over our twins
	for ( int32_t i = 0 ; i < nh ; i++ ) {
		// get host
		Host *h = &group[i];
		// skip if us
		if ( h == g_hostdb.m_myHost ) continue;
		// check the need to send req key now. the "a" key.
		k = makeKey(1,0,0,0,h->m_hostId,sid,zid,1);
		// get it
		nn = m_qt.getNode ( 0 , (char *)&k );
		// if there, we cannot delete it yet
		if ( nn >= 0 ) return false;
		// check the recv key now. the "b" key.
		k = makeKey(0,1,0,0,h->m_hostId,sid,zid,1);
		// get it
		nn = m_qt.getNode ( 0 , (char *)&k );
		// if there, we cannot delete it yet
		if ( nn >= 0 ) return false;
	}
	// i guess we can delete it, all keys are gone
	return true;
}




// . ***** SYNC LOOP *****
// . loop over the checkoff requests we are waiting for. "b" keys.
// . send out msg0 requests for meta lists we need
// . if we got a 60+ sec old checkoff recvd but never got the meta list
//   ourselves, then request it!!
bool Syncdb::loop4 ( ) {
	// make the start key. a "b" key
	key128_t k = m_syncKey;
	// end key
	key128_t ek;
	ek = makeKey(0,1,0,0,0xffffffff,0xffffffff,0xffffffffffffffffLL,1);
	// get next node
	int32_t nn = m_qt.getNode ( 0 , (char *)&k );
	// get group we are in
	//Host *group = g_hostdb.getMyShard();
	// use this for determining approximate age of meta lists
	int64_t nowms = gettimeofdayInMilliseconds();
	// do the loop
	for ( ; nn >= 0 ; nn = m_qt.getNextNode ( nn ) ) {
		// breathe
		QUICKPOLL ( MAX_NICENESS );
		// give other loops some time so we can digest these
		// meta lists we added
		if ( m_addCount >= 100 ) break;
		// get key
		key128_t k = *(key128_t *)m_qt.getKey ( nn );
		// stop when we hit the end
		if ( k > ek ) break;
		// update
		m_syncKey = k;
		// get twin id
		uint32_t tid = getTid ( &k );
		// skip if tid is dead
		if ( g_hostdb.isDead ( tid ) ) {
			// skip to next tid!
			tid++;
			// . find the key of the FIRST meta list we need to add
			//   for this new senderId, "sid"
			// . mdw: i reset sid to 0
			key128_t nk = makeKey ( 0,1,0,0,tid,0,0,0 );
			// undo the m_qt.getNextNode(nn) we call in for loop
			nn = m_qt.getPrevNode ( 0 , (char *)&nk );
			// sanity check
			if ( nn < 0 ) { char *xx=NULL;*xx=0; }
			// get next key from this new sid
			continue;
		}
		// . skip if delbit set
		// . the delbit set means we got the meta list and are 
		//   waiting to receive the checkoff request from twin #sid
		// . when it comes in it will annihilate with this key!
		// . right now we are only interested in *negative* keys, 
		//   those with their delbit cleared
		if ( (k.n0 & 0x01) == 0x01 ) continue;
		// . ok we got checkoff request from tid for this sid/zid
		// . and we have not got the meta list ourselves yet cuz
		//   we would have had k with the delbit set.
		// . get zid
		uint64_t zid = getZid ( &k );
		// get sid
		uint32_t sid = getSid ( &k );
		// get its approximate age
		int64_t age = nowms - zid;
		// go to next sid if not 60 seconds yet for this one
		if ( age < 60000 ) {
			// no use banging away at this sid any more since we
			// are missing another action for this one
			sid++;
			// find the key of the FIRST meta list we need to add
			// for this new senderId, "sid". make the "b" key.
			key128_t nk = makeKey ( 0,1,0,0,tid,sid,0,0 );
			// undo the m_qt.getNextNode(nn) we call in for loop
			nn = m_qt.getPrevNode ( 0 , (char *)&nk );
			// sanity check
			if ( nn < 0 ) { char *xx=NULL;*xx=0; }
			// get next key from this new sid
			continue;
		}
		// make the "d" key to see if we got the meta list just
		// to make sure! we do not want to re-add a meta list!
		key128_t dk = makeKey(0,0,0,1,0,sid,zid,1);
		if ( m_qt.getNode ( 0 , (char *)&dk ) >= 0 ) continue;
		// note it
		log("sync: requesting meta list sid=%"UINT32" zid=%"UINT64" age=%"UINT64" "
		    "from twin hostid #%"INT32"",
		    (uint32_t)sid,
		    (uint64_t)zid,
		    (uint64_t)age,
		    (int32_t)tid);
		// i guess we are out of sync
		g_hostdb.m_myHost->m_inSync = false;
		// do not let sleep ticker call bigLoop
		m_outstanding = true;
		// record the sid
		m_requestedSid = sid;
		// make the c key range
		key128_t sk2 = makeKey(0,0,1,0,0,sid,zid,0);
		key128_t ek2 = makeKey(0,0,1,0,0,sid,zid,1);
		// ok, ask tid for this meta list
		if ( ! m_msg0.getList ( tid             , // hostId
					0               , // ip
					0               , // port
					0               , // maxCacheAge
					false           , // addToCache
					RDB_SYNCDB      ,
					0            , // collnum
					&m_list         ,
					(char *)&sk2    ,
					(char *)&ek2    ,
					999             , // minRecSizes
					NULL            ,
					gotListWrapper4 ,
					MAX_NICENESS    ,
					false           ))// err correction?
			return false;
		// stop on error. return true with g_errno set on error
		if ( ! gotList4() ) return true;
	}
	// we completed
	m_calledLoop4 = true;
	return true;
}
void gotListWrapper4 ( void *state ) {
	// returns false if this had an error
	if ( ! g_syncdb.gotList4() ) return;
	// otherwise, resume the loop
	g_syncdb.loop4();
}

// returns false and sets g_errno on error
bool Syncdb::gotList4 ( ) {
	// bigLoop() can call us again now
	m_outstanding = false;
	// return false on error
	if ( g_errno ) return false;
	// should NOT be empty
	if ( m_list.isEmpty() ) { 
		// maybe this host is not in sync?
		g_errno = EBADENGINEER;
		log("sync: twin did not have zid but we had checkoff "
		    "request from twin.");
		return false;
	}
	// get the reply, should be a msg4 request
	char *req     = m_list.getCurrentData();
	int32_t  reqSize = m_list.getCurrentDataSize();
	// . return false if this had an error, g_errno should be set
	// . this will be just like we got the request from msg4 directly!
	if ( ! gotMetaListRequest ( req , reqSize , m_requestedSid ) ) 
		return false;
	// . count adds
	// . after 100 we move on to another loop so that we can digest
	//   these meta lists!
	m_addCount++;
	// advance the key. do not repeat this key since we were successful
	m_syncKey += 1;
	return true;
}












// . ***** CHECKOFF LOOP *****
// . loop over the checkoff requests TO SEND
// . send checkoff requests out that we need to send and have not yet got
//   a reply for. when we get a reply for them the "need to send checkoff 
//   request" key is immediately deleted
// . make a big list then call msg1 with an rdbId of RDB_SYNCDB
// . do not include dead hosts
void Syncdb::loop5 ( ) {
	// make the start key
	key128_t k = m_nextk;
	// end key
	key128_t ek;
	ek = makeKey(1,0,0,0,0xffffffff,0xffffffff,0xffffffffffffffffLL,1);
	// reset
	m_nk = 0;
	// get it
	int32_t nn = m_qt.getNextNode ( 0 , (char *)&k );
	// do one tid at a time
	uint32_t startTid = getTid ( &k );
	// do the loop
	for ( ; nn >= 0 ; nn = m_qt.getNextNode ( nn ) ) {
		// breathe
		QUICKPOLL ( MAX_NICENESS );
		// get key
		key128_t k = *(key128_t *)m_qt.getKey ( nn );
		// save it
		m_nextk = k;
		// add one
		m_nextk += 1;
		// stop when we hit the end
		if ( k > ek ) break;
		// get twin id
		uint32_t tid = getTid ( &k );
		// stop if tid changed
		if ( tid != startTid && m_nk >= 0 ) break;
		// skip if tid is dead
		if ( g_hostdb.isDead ( tid ) ) {
			// skip to next tid!
			tid++;
			// . find the key of the FIRST meta list we need to add
			//   for this new senderId, "sid"
			// . mdw: i reset sid to 0
			key128_t nk = makeKey ( 1,0,0,0,tid,0,0,0 );
			// undo the m_qt.getNextNode(nn) we call in for loop
			nn = m_qt.getPrevNode ( 0 , (char *)&nk );
			// bail if no good
			if ( nn < 0 ) break;
			// get next key from this new sid
			continue;
		}
		// get zid
		uint64_t zid = getZid ( &k );
		// get sid
		uint32_t sid = getSid ( &k );
		// note it
		log("sync: storing key for meta request list sid=%"UINT32" zid=%"UINT64" "
		    "from twin hostid #%"INT32"",(uint32_t)sid,
		    (uint64_t)zid,(int32_t)tid);
		// make the key. make NEGATIVE "b" keys.
		m_keys [ m_nk++ ] = makeKey ( 0,1,0,0,0,sid,zid,0 );
		// stop if full
		if ( m_nk >= MAX_CHECKOFF_KEYS ) break;
	}
	// all done?
	if ( k <= ek && m_nk <= 0 ) { m_calledLoop5 = true; return; }
	// . add the whole list at once
	// . make our own msg1 request to send to just one host
	if ( g_udpServer.sendRequest ( (char *)m_keys    ,
				       m_nk * 16         ,
				       0x5d              , // SYNCDB REQUEST
				       0                 , // ip
				       0                 , // port
				       startTid          , // hostId
				       NULL              , // retSlot
				       NULL              , // state
				       addedListWrapper5 , // wrapper
				       60                , // timeout
				       -1                , // backoff
				       -1                , // maxWait
				       NULL              , // replyBuf
				       0                 , // replyBufSize
				       MAX_NICENESS      ))
		// it blocked, return now
		return;
	// i guess we had an error...
	if ( ! addedList5() ) return;
}
void addedListWrapper5 ( void *state , UdpSlot *slot ) {
	// return on error
	if ( ! g_syncdb.addedList5() ) return;
	// otherwise, loop back up
	g_syncdb.loop5();
}
// yay, we got a reply to the checkoff request we sent
bool Syncdb::addedList5 ( ) {
	// are we missing a previous zid? then we are not in sync!
	//if ( g_errno == ENOSYNC ) g_hostdb.m_myHost->m_inSync = false;
	// error?
	if ( g_errno ) return false;
	// . remove from tree, we successfully added!
	// . make a fake list
	if ( ! m_qt.deleteKeys ( 0 , (char *)m_keys , m_nk ) ) {
		// sanity check - must all be in quicktree
		char *xx=NULL; *xx=0; }
	// success
	return true;
}
// . did we receive a checkoff request from a fellow twin?
// . request is a list of checkoff request keys ("a" keys)
void handleRequest5d ( UdpSlot *slot , int32_t netnice ) {
	// get the sending hostid
	int32_t sid = slot->m_hostId;
	// sanity check
	if ( sid < 0 ) { char *xx=NULL; *xx=0; }
	// get the request buffer
	//key128_t *keys = (key128_t *)slot->m_readBuf;
	int32_t      nk   = slot->m_readBufSize / 16;
	// int16_tcut
	UdpServer *us = &g_udpServer;
	// if tree gets full, then return false forever
	if ( ! g_syncdb.m_qt.hasRoomForKeys ( nk ) ) { 
		us->sendErrorReply ( slot , ETRYAGAIN );
		return; 
	}
	for ( int32_t i = 0 ; i < nk ; i++ ) {
		// get the key
		key128_t k = g_syncdb.m_keys[i];
		// sanity check. must be a negative key.
		if ( (k.n0 & 0x1) != 0x0 ) { char *xx=NULL;*xx=0; }
		// get the anti key. the "need to recv checkoff request"
		// key which is the positive
		key128_t pk = k;
		// make it positive
		pk.n0 |= 0x01;
		// is it in there?
		int32_t nn = g_syncdb.m_qt.getNode ( 0 , (char *)&pk );
		// if yes, nuke it. they annihilate.
		if ( nn >= 0 ) {
			g_syncdb.m_qt.deleteNode3 ( nn , true );
			continue;
		}
		// . otherwise, add right to the tree
		// . should always succeed!
		if ( g_syncdb.m_qt.addKey(&k)<0) { char *xx=NULL;*xx=0; }
	}
	// return empty reply to mean success
	us->sendReply_ass ( NULL , 0 , NULL , 0 , slot );
}





static void sleepWrapper ( int fd , void *state ) {
	g_syncdb.bigLoop();
}

void Syncdb::bigLoop ( ) {
	// try to launch our sync
	if ( m_doRcp ) rcpFiles();
	// we got a msg0 or msg5 outstanding. wait for it to come back first.
	if ( m_outstanding ) return;
	if ( ! m_calledLoop1 ) { loop1(); return; }
	if ( ! m_calledLoop2 ) { loop2(); return; }
	if ( ! m_calledLoop3 ) { loop3(); return; }
	if ( ! m_calledLoop4 ) { loop4(); return; }
	if ( ! m_calledLoop5 ) { loop5(); return; }
	// if done calling loop4(), reset this
	loopReset();
}

void Syncdb::loopReset() {
	m_outstanding = false;
	// reset flags
	m_calledLoop1 = false;
	m_calledLoop2 = false;
	m_calledLoop3 = false;
	m_calledLoop4 = false;
	m_calledLoop5 = false;
	m_syncKey     = makeKey(0,1,0,0,0,0,0,0);
	m_addCount    = 0;
	// if done calling loop5(), reset this
	m_nextk = makeKey ( 1,0,0,0,0,0,0,0 );
}








// reset rdb
void Syncdb::reset() { 
	m_rdb.reset(); 
	m_qt.reset();
}

bool Syncdb::registerHandlers ( ) {
	// register ourselves with the udp server
	if ( ! g_udpServer.registerHandler ( 0x5d, handleRequest5d ) )
		return false;
	// register ourselves with the udp server
	if ( ! g_udpServer.registerHandler ( 0x59, handleRequest55 ) )
		return false;
	return true;
}

bool Syncdb::init ( ) {
	// reset
	loopReset();
	m_doRcp      = false;
	m_rcpStarted = false;
	// setup quick tree
	if ( ! m_qt.set ( 0           , // fixedDataSize
			  300000      , // 300k nodes
			  true        , // balance?
			  -1          , // maxmem, no max
			  false       , // ownData?
			  "tresyncdb" ,
			  false       , // dataInPtrs?
			  "quicktree" , // dbname
			  16          , // keySize
			  false       ))// useProtection?
		return false;
	BigFile f;
	f.set ( g_hostdb.m_dir , "quicktree.dat" );
	// only load if it exists
	bool exists = f.doesExist();
	// load it
	if ( exists && ! m_qt.fastLoad( &f , &m_stack ) ) 
		return log("sync: quicktree.dat load failed: %s",
			   mstrerror(g_errno));
	// done
	f.close();
	// assume permanently out of sync
	int32_t val = 2;
	// load the insync.dat file
	f.set ( g_hostdb.m_dir , "insync.dat" );
	// fail on open failure
	if ( ! f.open ( O_RDONLY ) ) return false;
	// if not there, permanently out of sync
	if ( ! f.doesExist() )
		log("sync: insync.dat does not exist. Assuming host is "
		    "unrecoverable.");
	else {
		// get the value
		char buf[20];
		int32_t n = f.read ( &buf , 10 , 0 ) ;
		if ( n <= 0 )
			return log("sync: read insync.dat: %s",
				   mstrerror(g_errno));
		// must be digit
		if ( ! is_digit ( buf[0] ) )
			return log("sync: insync.dat has no number in it.");
		// unlink it
		if ( ! f.unlink() )
			return log("sync: failed to unlink insync.dat: %s",
				   mstrerror(g_errno));
		// get the value
		val = atol ( buf );
	}
	// bad val?
	if ( val < 0 || val > 2 ) 
		return log("sync: insync.dat had bad value of %"INT32"",val);
	// report if in sync or not
	if ( val == 0 ) log("sync: insync.dat says out of sync");
	if ( val == 1 ) log("sync: insync.dat says in sync");
	if ( val == 2 ) log("sync: insync.dat says PERMANENTLY out of sync");
	// set it
	Host *h = g_hostdb.m_myHost;
	if ( val == 1 ) h->m_inSync               = 1;
	if ( val == 2 ) h->m_isPermanentOutOfSync = 1;
	// call this once per second
	if ( ! g_loop.registerSleepCallback ( 1000 , NULL , sleepWrapper ) )
		return false;
	// 10 MB
	int32_t maxTreeMem = 10000000;
	// . what's max # of tree nodes?
	// . key+4+left+right+parents+dataPtr = 12+4 +4+4+4+4 = 32
	// . 28 bytes per record when in the tree
	int32_t maxTreeNodes  = maxTreeMem / ( 16 + 1000 );
	// . initialize our own internal rdb
	// . records are actual msg4 requests received from Msg4
	// . the key is formed calling Syncdb::makeKey() which is based on
	//   the tid, sid and zid of the msg4 request, where tid is the
	//   twin hostid we are chatting with in our group, sid is the 
	//   ORIGINAL sending hostid of the msg4 request, and zid is the
	//   kinda transaction #, and is unique.
	if ( ! m_rdb.init ( g_hostdb.m_dir ,
			    "syncdb"       ,
			    true           , // dedup
			    -1             , // dataSize is variable
			    50             , // min files to merge
			    maxTreeMem     ,
			    maxTreeNodes   , // maxTreeNodes  ,
			    true           , // balance tree?
			    50000          , // maxCacheMem   , 
			    100            , // maxCacheNodes ,
			    false          , // half keys?
			    false          ,  // save cache?
			    NULL           ,  // page cache
			    false          ,  // is titledb
			    false          ,  // preload disk page cache
			    16             ,  // key size
			    false          , // bias disk page cache?
			    true           ))// is collectionless?
		return false;
	// add the coll
	//if ( ! g_syncdb.m_rdb.addColl ( "dummy" ) ) return true;
	
	// reset quick tree?
	if ( ! h->m_isPermanentOutOfSync ) return true;
	// clear it all!
	m_qt.clear();
	// add the base since it is a collectionless rdb
	return m_rdb.addRdbBase1 ( NULL );
}

// . save our crap
// . returns false and sets g_errno on error
bool Syncdb::save ( ) {
	g_errno = 0;
	// save the quick tree
	m_qt.fastSave ( g_hostdb.m_dir ,
			"synctree"     ,
			false          ,
			NULL           ,
			NULL           );
	// error?
	if ( g_errno )
		return log("sync: save: %s",mstrerror(g_errno));
	// save the insync.dat file
	Host *h = g_hostdb.m_myHost;
	File f;
	f.set ( g_hostdb.m_dir , "insync.dat" );
	// open it
	if ( ! f.open ( O_RDWR | O_CREAT ) )
		return log("sync: open insync.dat: %s",mstrerror(g_errno));
	char buf[2];
	buf[0] = '0';
	buf[1] = '\0';
	if ( h->m_inSync               ) buf[0] = '1';
	if ( h->m_isPermanentOutOfSync ) buf[0] = '2';
	if ( f.write ( buf , 2 , 0 ) != 2 )
		return log("sync: write insync.dat: %s",mstrerror(g_errno));
	return true;
}

bool Syncdb::verify ( char *coll ) {
	log ( LOG_INFO, "db: Verifying Syncdb for coll %s...", coll );
	g_threads.disableThreads();

	Msg5 msg5;
	Msg5 msg5b;
	RdbList list;
	key_t startKey;
	key_t endKey;
	startKey.setMin();
	endKey.setMax();
	CollectionRec *cr = g_collectiondb.getRec(coll);
	
	if ( ! msg5.getList ( RDB_SYNCDB    ,
			      cr->m_collnum          ,
			      &list         ,
			      startKey      ,
			      endKey        ,
			      64000         , // minRecSizes   ,
			      true          , // includeTree   ,
			      false         , // add to cache?
			      0             , // max cache age
			      0             , // startFileNum  ,
			      -1            , // numFiles      ,
			      NULL          , // state
			      NULL          , // callback
			      0             , // niceness
			      false         , // err correction?
			      NULL          ,
			      0             ,
			      -1            ,
			      true          ,
			      -1LL          ,
			      &msg5b        ,
			      true          )) {
		g_threads.enableThreads();
		return log("db: HEY! it did not block");
	}

	int32_t count = 0;
	int32_t got   = 0;
	for ( list.resetListPtr() ; ! list.isExhausted() ;
	      list.skipCurrentRecord() ) {
		key_t k = list.getCurrentKey();
		count++;
		//uint32_t groupId = getGroupId ( RDB_SYNCDB , &k );
		//if ( groupId == g_hostdb.m_groupId ) got++;
		uint32_t shardNum = getShardNum ( RDB_SYNCDB , (char *)&k );
		if ( shardNum == getMyShardNum() ) got++;
	}
	if ( got != count ) {
		log ("db: Out of first %"INT32" records in syncdb, "
		     "only %"INT32" belong to our group.",count,got);
		// exit if NONE, we probably got the wrong data
		if ( got == 0 ) log("db: Are you sure you have the "
					   "right "
					   "data in the right directory? "
					   "Exiting.");
		log ( "db: Exiting due to Syncdb inconsistency." );
		g_threads.enableThreads();
		return g_conf.m_bypassValidation;
	}
	log ( LOG_INFO, "db: Syncdb passed verification successfully for "
			"%"INT32" recs.", count );
	// DONE
	g_threads.enableThreads();
	return true;
}

// . returns false on error
// . called from PageHosts.cpp!!!
bool Syncdb::syncHost ( int32_t syncHostId ) {
	Host *sh = g_hostdb.getHost ( syncHostId );
	if ( ! sh ) return log("sync: bad host id %"INT32"",syncHostId);
	// get its group
	//Host *hosts = g_hostdb.getGroup ( sh->m_groupId );
	Host *hosts = g_hostdb.getShard ( sh->m_shardNum );
	// get the best twin for it to sync from
	for ( int32_t i = 0 ; i < g_hostdb.getNumHostsPerShard() ; i++ ) {
		// get host
		Host *h = &hosts[i];
		// skip if dead
		if ( g_hostdb.isDead ( h ) ) continue;
		// skip if permanent out of sync
		if ( h->m_isPermanentOutOfSync ) continue;
		// not itself! it must be dead... wtf!?
		if ( h == sh ) continue;
		// save it
		int32_t tmp = syncHostId;
		// log it
		log("sync: sending sync request to host id #%"INT32"",h->m_hostId);
		// int16_tcut
		UdpServer *us = &g_udpServer;
		// use that guy
		if ( us->sendRequest ( (char *)&tmp      ,
				       4                 ,
				       0x59              , // SYNCDB REQUEST
				       0                 , // ip
				       0                 , // port
				       h->m_hostId       , // hostId
				       NULL              , // retSlot
				       NULL              , // state
				       gotReplyWrapper55 , // wrapper
				       15                , // timeout
				       -1                , // backoff
				       -1                , // maxWait
				       NULL              , // replyBuf
				       0                 , // replyBufSize
				       MAX_NICENESS      ))
			// success
			return true;
		// note it
		log("sync: had error sending sync request to host id #%"INT32": %s",
		    h->m_hostId,mstrerror(g_errno));
		// error!
		return false;
	}
	// none to sync from
	return log("sync: could not find adequate twin to sync from!");
}
void gotReplyWrapper55 ( void *state , UdpSlot *slot ) {
	if ( g_errno ) 
		log("sync: got error reply from sync host: %s",
		    mstrerror(g_errno));
	else
		log("sync: got success reply from sync host");
}
// rcp our files to a new host (was a spare)
void handleRequest55 ( UdpSlot *slot , int32_t netnice ) {
	// who should we copy to?
	char *p    = slot->m_readBuf;
	int32_t  size = slot->m_readBufSize;
	UdpServer *us = &g_udpServer;
	// get hostid
	int32_t hostId = -1;
	if ( size == 4 ) hostId = *(int32_t *)p;
	// panic?
	if ( hostId < 0 || hostId >= g_hostdb.getNumHosts() ) {
		us->sendErrorReply ( slot , EBADENGINEER );
		return; 
	}
	// set this. RdbBase will turn off all merging
	Host *h = g_hostdb.getHost ( hostId );
	// only one at a time now!
	if ( g_hostdb.m_syncHost ) { 
		log ( "sync: Already syncing with another host. Aborting.");
		us->sendErrorReply ( slot , EBADENGINEER );
		return; 
	}
	// can't sync with ourselves
	if ( h == g_hostdb.m_myHost ) { 
		log ( "sync: Cannot sync with yourself. Aborting.");
		us->sendErrorReply ( slot , EBADENGINEER );
		return; 
	}
	if ( ! g_hostdb.isDead ( h ) ) {
		log ( "sync: Cannot sync live host. Aborting.");
		us->sendErrorReply ( slot , EBADENGINEER );
		return; 
	}
	Host *me = g_hostdb.m_myHost;
	if ( me->m_isPermanentOutOfSync ) {
		log ( "sync: Src host is permanently out of sync. Aborting.");
		us->sendErrorReply ( slot , EBADENGINEER );
		return; 
	}
	// now check it for a clean directory
	int32_t ip = h->m_ip;
	// if that is dead use ip #2
	if ( h->m_ping        >= g_conf.m_deadHostTimeout &&
	     h->m_pingShotgun <  g_conf.m_deadHostTimeout   ) 
		ip = h->m_ipShotgun;
	char *ips = iptoa(ip);
	char cmd[1024];
	sprintf ( cmd, "ssh %s \"cd %s; du -b | tail -n 1\" > ./synccheck.txt",
		  ips, h->m_dir );
	log ( LOG_INFO, "init: %s", cmd );
	system(cmd);
	int32_t fd = open ( "./synccheck.txt", O_RDONLY );
	if ( fd < 0 ) {
		log( "sync: Unable to open synccheck.txt. Aborting.");
		us->sendErrorReply ( slot , EBADENGINEER );
		return; 
	}

	int32_t len = read ( fd, cmd, 1023 );
	cmd[len] = '\0';
	close(fd);
	// delete the file to make sure we don't reuse it
	system ( "rm ./synccheck.txt" );
	// check the size
	int32_t checkSize = atol(cmd);
	if ( checkSize > 4096 || checkSize <= 0 ) {
		log("sync: Detected %"INT32" bytes in directory to "
		    "sync.  Must be empty.  Is .antiword dir in "
		    "there?", checkSize);
		us->sendErrorReply ( slot , EBADENGINEER );
		return; 
	}
	// RdbBase will not start any new merges if this m_syncHost is set
	g_hostdb.m_syncHost = h;
        // set this flag
	g_hostdb.m_syncHost->m_doingSync = 1;
}

void Syncdb::rcpFiles ( ) {

	// wait for all merging to finish
	if ( g_merge.isMerging() || g_merge2.isMerging() ) return;

	// if first time, save it
	if ( ! m_rcpStarted ) {
		// do not re-call it this time
		m_rcpStarted = true;
		// note it
		log("sync: Saving all files before rcp'ing to twin.");
		// save us. return if blocked. we will be re-called
		if ( ! g_process.save() ) return;
	}

	// log the start
	log ( LOG_INFO, "sync: Copying our data files to host %"INT32".", 
	      g_hostdb.m_syncHost->m_hostId );

	// start the sync in a thread, complete when it's done
	if ( g_threads.call ( GENERIC_THREAD     ,
			      MAX_NICENESS       ,
			      this               ,
			      syncDoneWrapper    ,
			      syncStartWrapper_r ) ) 
		// return on successful launch
		return ;
	// error. 
	g_hostdb.m_syncHost->m_doingSync = 0;
	g_hostdb.m_syncHost              = NULL;
	m_doRcp      = false;
	m_rcpStarted = false;
	log ( "sync: Could not spawn thread for call to sync "
	      "host. Aborting." );
}

void syncDoneWrapper ( void *state , ThreadEntry *te ) {
	Syncdb *THIS = (Syncdb*)state;
	THIS->syncDone();
}

void *syncStartWrapper_r ( void *state , ThreadEntry *te ) {
	Syncdb *THIS = (Syncdb*)state;
	THIS->syncStart_r(true);
	return NULL;
}

//int my_system_r ( char *cmd , int32_t timeout );
int startUp ( void *cmd );

void Syncdb::syncStart_r ( bool amThread ) {

	// turn this off
	g_process.m_suspendAutoSave = true;

	char cmd[1024];
	// get synchost best ip
	char *ips = iptoa ( g_hostdb.getAliveIp ( g_hostdb.m_syncHost ) );
	// his dir
	char *dir = g_hostdb.m_syncHost->m_dir;
	// use
	Host *me = g_hostdb.m_myHost;
	// ours
	char *mydir = me->m_dir;
	// generic
	int32_t err;

	// loop over every rdb and every data and map file in each rdb
	for ( int32_t i = 0 ; i < RDB_END ; i++ ) {

	// skip SYNCDB
	if  ( i == RDB_SYNCDB ) continue;
	// get that rdb
	Rdb *rdb = getRdbFromId ( i );
	// skip if none
	if ( ! rdb ) continue;

	// get coll
	for ( int32_t j = 0 ; j < rdb->getNumBases() ; j++ ) {

		// get that base
		RdbBase *base = rdb->getBase(j);//m_bases[j];
		if ( ! base ) continue;

	// get coll
	char *coll = base->m_coll;
	// and num
	int32_t collnum = base->m_collnum;
	// make the dir
	sprintf ( cmd , "ssh %s 'mkdir %scoll.%s.%"INT32"'",
		  ips,dir,coll,collnum);
	// excecute
	log ( LOG_INFO, "sync: %s", cmd );
	//int err = my_system_r ( cmd, 3600*24 );
	//if ( err != 0 ) goto hadError;

	// copy the files
	for ( int32_t k = 0 ; k < base->m_numFiles ; k++ ) {

	// sleep while dumping. we are in a thread.
	if ( base->isDumping() ) sleep ( 1 );


	// get map
	RdbMap *map = base->m_maps[k];
	// copy the map file
	sprintf ( cmd , "rcp %s %s:%scoll.%s.%"INT32"/'",
		  map->getFilename(),ips,dir,coll,collnum);
	log ( LOG_INFO, "sync: %s", cmd );
	// MDW: take out for now
	//if ( ( err = my_system_r ( cmd, 3600*24 ) ) ) goto hadError;
	
	// get the file
	BigFile *f = base->m_files[k];

	// loop over each little part file
	for ( int32_t m = 0 ; m < f->m_numParts ; m++ ) {

	// get part file
	File *p = f->getFile2(m);//m_files[m];
	// copy that
	sprintf ( cmd , "rcp %s %s:%scoll.%s.%"INT32"/'",
		  p->getFilename(),ips,dir,coll,collnum);
	// excecute
	log ( LOG_INFO, "sync: %s", cmd );
	// MDW: take out for now
	//if ( ( err = my_system_r ( cmd, 3600*24 ) ) ) goto hadError;

	}
	}
	}
	}

	// make the dirs
	sprintf ( cmd , "ssh %s '"
		  "mkdir %s/dict/ ;"
		  "mkdir %s/dict/en/ ;"
		  "mkdir %s/ucdata/ ;"
		  "mkdir %s/.antiword/ ;"
		  "'" ,
		  ips,
		  dir,
		  dir,
		  dir,
		  dir
		  );
	// excecute
	log ( LOG_INFO, "sync: %s", cmd );
	// MDW: take out for now
	//if ( ( err = my_system_r ( cmd, 3600*24 ) ) ) goto hadError;


	// loop over the files in Process.cpp
	for ( int32_t i = 0 ; i < 99999 ; i++ ) {
		// null means end
		if ( ! g_files[i] ) break;
		sprintf ( cmd , "rcp %s%s %s:%s",
			  mydir,g_files[i],ips,dir);
		// excecute
		log ( LOG_INFO, "sync: %s", cmd );
		// MDW: take out for now
		//if ( ( err = my_system_r ( cmd, 3600*24 ) ) ) goto hadError;
	}

	// new guy is NOT in sync
	sprintf ( cmd , "ssh %s 'echo 0 > %sinsync.dat", ips,dir);
	// excecute
	log ( LOG_INFO, "sync: %s", cmd );
	// MDW: take out for now
	//if ( ( err = my_system_r ( cmd, 3600*24 ) ) ) goto hadError;

	// saved files
	sprintf ( cmd , "rcp %s*-saved.dat %s:%sinsync.dat", 
		  mydir,ips,dir);
	// excecute
	log ( LOG_INFO, "sync: %s", cmd );
	// MDW: take out for now
	//if ( ( err = my_system_r ( cmd, 3600*24 ) ) ) goto hadError;
	
	// completed!
	return;

	// hadError:
	log ( "sync: Call to system(\"%s\") had error %s.",cmd,strerror(err));
	g_hostdb.m_syncHost->m_doingSync = 0;
	g_hostdb.m_syncHost              = NULL;
	return;
}

void Syncdb::syncDone ( ) {
	// allow autosaves again
	g_process.m_suspendAutoSave = false;
	// now make a call to startup the newly synced host
	if ( ! g_hostdb.m_syncHost ) {
		log ( "sync: SyncHost is invalid. Most likely a problem "
		      "during the sync. Ending synchost." );
		return;
	}
	log ( LOG_INFO, "init: Sync copy done.  Starting host." );
	g_hostdb.m_syncHost->m_doingSync = 0;
	char cmd[1024];
	sprintf(cmd, "./gb start %"INT32"", g_hostdb.m_syncHost->m_hostId);
	log ( LOG_INFO, "init: %s", cmd );
	system(cmd);
	g_hostdb.m_syncHost = NULL;
	m_doRcp      = false;
	m_rcpStarted = false;
}

/*
// TODO: Provide verification.
bool Syncdb::addColl ( char *coll, bool doVerify ) {
	if ( ! m_rdb.addColl ( coll ) ) return false;
	return true;
}
*/
