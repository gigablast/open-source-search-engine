#include "RequestTable.h"


RequestTable::RequestTable ( ) {
	m_bufSize = 2 * HT_BUF_SIZE;
	m_htable.set ( 50 , m_buf, m_bufSize, true ); // allow dup keys?
	m_processHash = 0;
}

RequestTable::~RequestTable ( ) {
	reset();
}

void RequestTable::reset ( ) {
	m_htable.reset();
	m_processHash = 0;
}

long RequestTable::addRequest ( long long requestHash , void *state2 ) {
	// sanity check
	if ( requestHash == 0 ){
		char *xx = NULL; *xx = 0;
	}

	// check if we have the state already in the hashtable
	/*long n = m_htable.getOccupiedSlotNum ( requestHash );
	while ( m_htable.m_keys[n] ){
		// count if same key
		if ( m_htable.m_keys[n] == requestHash &&
		     m_htable.m_vals[n] == ( long ) state2 ){
			char *xx = NULL; *xx = 0;
		}
		// advance n, wrapping if necessary
		if ( ++n >= m_htable.m_numSlots ) n = 0;
		}*/

	// returns false and set g_errno on error, so we should return -1
	if ( ! m_htable.addKey(requestHash, (long)state2) ) return -1;

 	//log ( "requesttable: added hash=%lli state2=%lx", requestHash,
	//     (long) state2 );
	// count the slots that have this key
	long n = m_htable.getOccupiedSlotNum ( requestHash );
	// sanity check
	if ( n < 0 ) { char *xx = NULL; *xx = 0; }
	
	// return more than 1 if we are processing the same hash in gotReply
	// gotReply shall call it eventually so no need to send udpServer
	if ( m_processHash == requestHash )
		return 2;

	// count how many of our key are in the table, since we allow dup keys
	long count = 0;

	while ( m_htable.m_keys[n] ){
		// count if same key
		if ( m_htable.m_keys[n] == requestHash ) count++;
		// advance n, wrapping if necessary
		if ( ++n >= m_htable.m_numSlots ) n = 0;
	}
	/*if ( count == 1 )
		log( "requesttable: hash=%lli state2=%lx is getting quality",
		requestHash, (long) state2 );*/
	return count;
}

void RequestTable::gotReply ( long long  requestHash ,
			      char      *reply       ,
			      long       replySize   ,
			      void      *state1      ,
			      void     (*callback)( char *reply     ,
						    long  replySize ,
						    void *state1    ,
						    void *state2    )){
	// sanity check. 
	// We should never get a call when we are processing the request.
	if ( m_processHash != 0 ){
		char *xx = NULL; *xx = 0;
	}
	// lock the hashtable by adding the current key 
	m_processHash = requestHash;

	// save g_errno in case callback resets it
	long saved = g_errno;
	long n = m_htable.getOccupiedSlotNum ( requestHash );
	while ( n >= 0 ) {
		// restore it before returning
		g_errno = saved;
		
		// state2 is in the table
		void *state2 = (void *)m_htable.m_vals[n];
		// remove from table BEFORE calling callback in case callback
		// somehow alters the table!
		//m_htable.removeKey ( requestHash );
		m_htable.removeSlot ( n );
		/*log ( "requesttable: removed hash=%lli state1=%lx state2=%lx", 
		  requestHash, (long) state1, (long) state2 );*/

		// restore it before calling callback
		g_errno = saved;
		// otherwise, it is, call callback
		callback ( reply , replySize , state1 , state2 );
		// get next
		n = m_htable.getOccupiedSlotNum ( requestHash );
	}
	// all done, unlock the hash table.
	m_processHash = 0;
	return;
}

void RequestTable::cancelRequest ( long long requestHash , void *state2 ) {
	// there should only be one request for this request hash
	long n = m_htable.getOccupiedSlotNum ( requestHash );
	if ( n < 0 ){
		char *xx = NULL; *xx = 0;
	}
	m_htable.removeKey(requestHash);
	// check if there is any other remaining. core if there is
	n = m_htable.getOccupiedSlotNum ( requestHash );
	if ( n >= 0 ){
		char *xx = NULL; *xx = 0;
	}
	log( LOG_INFO, "reqtable: cancelled request hash=%lli state2=%lx", 
	     requestHash, (long) state2 );
	return;
}
