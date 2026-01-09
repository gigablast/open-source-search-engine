// Matt Wells, copyright Oct 2001

// . used for looking up IndexLists for queries
// . call init() to get initial read info per IndexList (1 per termId in query)
// . call update() to update read info for next read of lists
// . use getStartKey() , getEndKey(), getNumRecsToRead() to extract read info
// . tries to keep the amount of reading to a minimal
// . if # of results is not achieved the call update() to get read info for
//   another read to hopefully get the # of requested docIds

#ifndef _INDEXREADINFO_H_
#define _INDEXREADINFO_H_

#include "Query.h"  // MAX_QUERY_TERMS
#include "IndexList.h"
#include "Titledb.h"
#include "Indexdb.h"

// how many tiered might we break an indexlist into?
#define MAX_TIERS 3

// . define read sizes of each stage
// . each docid is 6 bytes, but first is 12
// . stage0 was 5000, but made it 8000 for trek today,
// . let's see how the powers of ten perform
#define STAGE0 (10000   *6)
#define STAGE1 (100000  *6)
#define STAGE2 (1000000 *6)
#define STAGESUM (STAGE0 + STAGE1 + STAGE2) // + STAGE3)

class IndexReadInfo {

 public:

	// just sets m_numLists to 0
	IndexReadInfo();

	// . this will calculate minStartKey and maxEndKey for each termId
	// . does not copy these, so don't trash this stack
	// . "stage0" is the first # of docIds to read from each IndexList
	//   -- dynamic truncation
	void init ( Query *q , 
		    int64_t *termFreqs ,
		    int32_t docsWanted , char callNum , int32_t stage0 ,
		    int32_t *tierStage ,
		    bool useDateLists ,
		    bool sortByDate , 
		    uint32_t date1 , 
		    uint32_t date2 ,
		    bool isDebug );

	// . this updates the start keys and docsToRead for each list
	//   in preparation for another read
	// . call this after you've done a read and called 
	//   IndexTable::addLists() so it can hash them and calculate the #
	//   of results it got
	// . it advances m_startKey[i] to lastKey + 1 in lists[i]
	void update ( IndexList *lists , int32_t numLists , char callNum );

	void update2 ( int32_t tier ) ;

	/*	void updateForMsg3b ( char *lastParts,
			      int64_t *termFreqs, 
			      int32_t numLists );*/

	void update ( int64_t *termFreqs,
		      int32_t numLists,
		      char callNum );

	// update without the full lists, just the last part and size
	void update ( char *lastParts,
		      int32_t *listSizes,
		      int32_t numLists );

	// call this after calling update to determine read info per list
	char *getStartKeys  ( ) { return (char *)m_startKeys  ; };
	char *getEndKeys    ( ) { return (char *)m_endKeys    ; };
	char  getIgnored    ( int32_t i ) { return m_ignore[i]   ; };

	char  getHalfKeySize( ) { return m_hks        ; };
	
	// getting info directly, like above
	int32_t   getReadSize ( int32_t i ) { return m_readSizes[i]; };

	int32_t  *getReadSizes(        ) { return m_readSizes; };

	// . did we get the # of required results
	// . or are all our lists exhausted?
	// . call only AFTER calling update() above
	bool isDone ( ) { return m_isDone ; };

	// call only after calling init() to estimate # of results
	int64_t getEstimatedTotalHits();

	int32_t getNumLists () { return m_numLists; };

	int32_t getStage0Default ( ) ;

 private:

	// . reading positions to read next portion of each list
	// . set initially by init()
	// . updated by addLists
	// . might read one list multiple tims if we don't get enough hits
	//key_t m_startKeys   [ MAX_QUERY_TERMS ]; 
	//key_t m_endKeys     [ MAX_QUERY_TERMS ]; 
	//key128_t m_startKeys2  [ MAX_QUERY_TERMS ]; 
	//key128_t m_endKeys2    [ MAX_QUERY_TERMS ]; 
	char m_startKeys   [ MAX_QUERY_TERMS * MAX_KEY_BYTES ]; 
	char m_endKeys     [ MAX_QUERY_TERMS * MAX_KEY_BYTES ]; 
	// how many docIds/recs/keys should we read?
	int32_t  m_readSizes   [ MAX_QUERY_TERMS ];
	char  m_ignore      [ MAX_QUERY_TERMS ];

	// . the query we're doing
	// . the above arrays are 1-1 with the arrays in m_q, 1 for each termId
	Query *m_q;

	// how many index lists we're reading
	int32_t  m_numLists;

	// may be set to true after update() is called
	bool  m_isDone;

	// . for dynamic truncation, first # of docs to read from each list
	// . stages can now be set dynamically on a per query basis
	int32_t  m_stage[MAX_TIERS];
	//int32_t  m_stageSum;

	char  m_ks;
	char  m_hks;
	char  m_useDateLists;
        char  m_sortByDate;
	uint32_t m_date1;
	uint32_t m_date2;

	bool m_isDebug;
};

#endif



