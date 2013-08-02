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
	//long   numKeys = RdbList::getListSize() / sizeof(key_t);
	// loop thru each key and clear it's del bit
	//for ( long i = 0 ; i < numKeys ; i++ ) 
	//	keys[i].n0 &= 0xfffffffffffffffeLL;
	char *p    = m_list;
	char *pend = m_list + m_listSize;
	long  step = m_ks;
	for ( ; p < pend ; p += step ) *p &= 0xfe;
}

void IndexList::print() { 
	if ( m_ks==16 ) logf(LOG_DEBUG,"db:      termId date    score docId");
	else            logf(LOG_DEBUG,"db:      termId score docId");
	long i = 0;
	for ( resetListPtr() ; ! isExhausted() ; skipCurrentRecord() ) {
		// print out date lists here
		if ( m_ks == 16 ) {
			logf(LOG_DEBUG,
			     "db: %04li) %020lli %10lu %03li %020lli",
			     i++ ,
			     getTermId16(m_listPtr),
			     getCurrentDate(),
			     (long)getCurrentScore(),
			     (long long)getCurrentDocId() );
			continue;
		}
		logf(LOG_DEBUG,"db: %04li) %020lli %03li %020lli" ,
		     i++ ,
		     (long long)getCurrentTermId12() ,
		     (long)getCurrentScore(),
		     (long long)getCurrentDocId() );
	}
}
