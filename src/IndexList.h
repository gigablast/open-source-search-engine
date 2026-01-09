// Matt Wells, copyright Jun 2001

// . IndexList is a list of keys 
// . some keys are 12 bytes, some are 6 bytes (compressed)
// . see Indexdb.h for format of the keys

// . you can set from a TitleRec/SiteRec pair
// . you can set from a TermTable
// . we use this class to generate the final indexList for a parsed document
// . we have to do some #include's in the .cpp cuz the TitleRec contains an
//   IndexList for holding indexed link Text
// . the TermTable has an addIndexList() function 
// . we map 32bit scores from a TermTable to 8bits scores by taking the log
//   of the score if it's >= 128*256, otherwise, we keep it as is


// override all funcs in RdbList where m_useShortKeys is true...
// skipCurrentRec() etc need to use m_useHalfKeys in RdbList cuz
// that is needed by many generic routines, like merge_r, RdbMap, Msg1, Msg22..
// We would need to make it a virtual function which would slow things down...
// or make those classes have specialized functions for IndexLists... in 
// addition to the RdbLists they already support

#ifndef _INDEXLIST_H_
#define _INDEXLIST_H_

#include "RdbList.h"
//#include "SiteRec.h"
//#include "TermTable.h" // for setting from TitleRec/SiteRec
#include "Indexdb.h"   // g_indexdb.makeKey()

class IndexList : public RdbList {

 public:

	// why do i have to repeat this for LinkInfo::set() calling our set()??
	void set ( char *list , int32_t  listSize  , bool  ownData   ) {
		RdbList::set ( list     ,
			       listSize ,
			       list     , // alloc
			       listSize , // alloc size
			       0        , // fixed data size
			       ownData  ,
			       true     , // use half keys?
			       sizeof(key_t));// 12 bytes per key
	};

	// . set from a termtable and old IndexList (can be NULL)
	// . oldList is subtracted from this list
	/*
	bool set ( class TermTable    *table       ,
		   int64_t           docId       ,
		   class IndexList    *oldList     ,
		   class IndexList    *newDateList ,
		   int32_t                newDate     ,
		   class IndexList    *oldDateList ,
		   class Sections     *newSections ,
		   class Sections     *oldSections ,
		   uint64_t *chksum1Ptr  , // = NULL,
		   int32_t                niceness    ); // = 2);
	bool subtract ( TermTable *ourTable , class IndexList *oldList1 );
	*/


	// clear the low bits on the keys so terms are DELETED
	void clearDelBits ( );

	void print();

	//unsigned char score32to8 ( uint32_t score ) ;
	//static uint32_t score8to32(unsigned char score8);

	// . these are made for special IndexLists, too
	// . getTermId() assumes as 12 byte key
	int64_t getCurrentTermId12 ( ) {
		return getTermId12 ( m_listPtr ); };
	int64_t getTermId12 ( char *rec ) {
		return (*(uint64_t *)(&rec[4])) >> 16 ;
	};
	int64_t getTermId16 ( char *rec ) {
		return (*(uint64_t *)(&rec[8])) >> 16 ;
	};
	//int64_t getTermId12 ( char *rec ) {
	//	return ((int64_t)(*(uint32_t *)(m_listPtrHi+2))<<14) | 
	//		((*(uint16_t *)(m_listPtrHi))>>2) ;
	//};
	// these 2 assume 12 and 6 byte keys respectively
	int64_t getCurrentDocId () {
		if ( isHalfBitOn ( m_listPtr ) ) return getDocId6 (m_listPtr);
		else                             return getDocId12(m_listPtr);
	};
	int64_t getDocId ( char *rec ) {
		if ( isHalfBitOn ( rec ) ) return getDocId6 (rec);
		else                       return getDocId12(rec);
	};
	int64_t getCurrentDocId12 ( ) {
		return getDocId12 ( m_listPtr ); };
	int64_t getDocId12 ( char *rec ) {
		return ((*(uint64_t *)(rec)) >> 2) & DOCID_MASK; };
	//int64_t getDocId12 ( char *rec ) {
	//	((*(uint32_t *)rec)>>10) |
	//		(((int64_t)(*(uint16_t *)(rec+4)))<<22);
	//};
	int64_t getDocId6 ( char *rec ) {
		int64_t docid;
		*(int32_t *)(&docid) = *(int32_t *)rec;
		((char *)&docid)[4] = rec[4];
		docid >>= 2;
		return docid & DOCID_MASK;
	};
	// this works with either 12 or 6 byte keys
	unsigned char getCurrentScore ( ) {
		return getScore(m_listPtr); };
	unsigned char getScore ( char *rec ) { return ~rec[5]; };

	// uncomplemented...
	void setScore ( char *rec , char score ) { rec[5] = score; };

	// for date lists only...
	int32_t getCurrentDate ( ) { return ~*(int32_t *)(m_listPtr+6); };
};

#endif



