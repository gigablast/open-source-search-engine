#include "gb-include.h"

#include "IndexList.h"
#include <math.h>       // log() math functions
#include "Datedb.h"    // g_datedb

// . clear the low bits on the keys so terms are DELETED
// . used by Msg14 to delete a document completely from the index
void IndexList::clearDelBits ( ) {
	// get the list (may be the whole list, m_list)
	//key_t *keys = (key_t *) RdbList::getList();
	// . how many keys do we have?
	// . all keys should be 12 bytes since we don't repeat the termId
	//int32_t   numKeys = RdbList::getListSize() / sizeof(key_t);
	// loop thru each key and clear it's del bit
	//for ( int32_t i = 0 ; i < numKeys ; i++ ) 
	//	keys[i].n0 &= 0xfffffffffffffffeLL;
	char *p    = m_list;
	char *pend = m_list + m_listSize;
	int32_t  step = m_ks;
	for ( ; p < pend ; p += step ) *p &= 0xfe;
}

void IndexList::print() { 
	if ( m_ks==16 ) logf(LOG_DEBUG,"db:      termId date    score docId");
	else            logf(LOG_DEBUG,"db:      termId score docId");
	int32_t i = 0;
	for ( resetListPtr() ; ! isExhausted() ; skipCurrentRecord() ) {
		// print out date lists here
		if ( m_ks == 16 ) {
			logf(LOG_DEBUG,
			     "db: %04"INT32") %020"INT64" "
			     "%10"UINT32" %03"INT32" %020"INT64"",
			     i++ ,
			     getTermId16(m_listPtr),
			     (int32_t)getCurrentDate(),
			     (int32_t)getCurrentScore(),
			     (int64_t)getCurrentDocId() );
			continue;
		}
		logf(LOG_DEBUG,"db: %04"INT32") %020"INT64" "
		     "%03"INT32" %020"INT64"" ,
		     i++ ,
		     (int64_t)getCurrentTermId12() ,
		     (int32_t)getCurrentScore(),
		     (int64_t)getCurrentDocId() );
	}
}
