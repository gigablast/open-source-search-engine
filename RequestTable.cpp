#include "RequestTable.h"


RequestTable::RequestTable ( ) {
	m_bufSize = 2 * HT_BUF_SIZE;
	//m_htable.set ( 50 , m_buf, m_bufSize, true ); // allow dup keys?
	m_processHash = 0;
}

RequestTable::~RequestTable ( ) {
	reset();
}

void RequestTable::reset ( ) {
	m_htable.reset();
	m_processHash = 0;
}

int32_t RequestTable::addRequest ( int64_t requestHash , void *state2 ) {
	// sanity check
	if ( requestHash == 0 ){
		char *xx = NULL; *xx = 0;
	}

	if ( m_htable.m_ks == 0 ) {
		//HashTableT <int64_t,int32_t> m_htable;
		// allow dups!
		m_htable.set(8,sizeof(char *),50,m_buf,m_bufSize,
			     true,0,"rqstbl");
	}

	// check if we have the state already in the hashtable
	/*int32_t n = m_htable.getSlot ( requestHash );
	while ( m_htable.m_keys[n] ){
		// count if same key
		if ( m_htable.m_keys[n] == requestHash &&
		     m_htable.m_vals[n] == ( int32_t ) state2 ){
			char *xx = NULL; *xx = 0;
		}
		// advance n, wrapping if necessary
		if ( ++n >= m_htable.m_numSlots ) n = 0;
		}*/

	// returns false and set g_errno on error, so we should return -1
	if ( ! m_htable.addKey(&requestHash, &state2) ) return -1;

 	//log ( "requesttable: added hash=%"INT64" state2=%"XINT32"", requestHash,
	//     (int32_t) state2 );
	// count the slots that have this key
	int32_t n = m_htable.getSlot ( &requestHash );
	// sanity check
	if ( n < 0 ) { char *xx = NULL; *xx = 0; }
	
	// return more than 1 if we are processing the same hash in gotReply
	// gotReply shall call it eventually so no need to send udpServer
	if ( m_processHash == requestHash )
		return 2;

	// count how many of our key are in the table, since we allow dup keys
	int32_t count = 0;

	while ( m_htable.m_flags[n]) { // m_keys[n] ){
		// count if same key
		if ( *(int64_t *)m_htable.getValueFromSlot(n) == requestHash )
			count++;
		// advance n, wrapping if necessary
		if ( ++n >= m_htable.m_numSlots ) n = 0;
	}
	/*if ( count == 1 )
		log( "requesttable: hash=%"INT64" state2=%"XINT32" is getting quality",
		requestHash, (int32_t) state2 );*/
	return count;
}

void RequestTable::gotReply ( int64_t  requestHash ,
			      char      *reply       ,
			      int32_t       replySize   ,
			      void      *state1      ,
			      void     (*callback)( char *reply     ,
						    int32_t  replySize ,
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
	int32_t saved = g_errno;
	int32_t n = m_htable.getSlot ( &requestHash );
	while ( n >= 0 ) {
		// restore it before returning
		g_errno = saved;
		
		// state2 is in the table
		//void *state2 = (void *)m_htable.m_vals[n];
		void *state2 = *(void **)m_htable.getValueFromSlot(n);
		// remove from table BEFORE calling callback in case callback
		// somehow alters the table!
		//m_htable.removeKey ( requestHash );
		m_htable.removeSlot ( n );
		/*log ( "requesttable: removed hash=%"INT64" state1=%"XINT32" state2=%"XINT32"", 
		  requestHash, (int32_t) state1, (int32_t) state2 );*/

		// restore it before calling callback
		g_errno = saved;
		// otherwise, it is, call callback
		callback ( reply , replySize , state1 , state2 );
		// get next
		n = m_htable.getSlot ( &requestHash );
	}
	// all done, unlock the hash table.
	m_processHash = 0;
	return;
}

void RequestTable::cancelRequest ( int64_t requestHash , void *state2 ) {
	// there should only be one request for this request hash
	int32_t n = m_htable.getSlot ( &requestHash );
	if ( n < 0 ){
		char *xx = NULL; *xx = 0;
	}
	m_htable.removeKey(&requestHash);
	// check if there is any other remaining. core if there is
	n = m_htable.getSlot ( &requestHash );
	if ( n >= 0 ){
		char *xx = NULL; *xx = 0;
	}
	log( LOG_INFO, "reqtable: cancelled "
	     "request hash=%"INT64" state2=%"PTRFMT"", 
	     requestHash, (PTRTYPE) state2 );
	return;
}
