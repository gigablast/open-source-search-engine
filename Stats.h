// Matt Wells, copyright Jul 2002

// . all disk accesses should use this global class to record their stats
// . this class can dump a gif graph to disk in the html directory with
//   the filename "Stats.gif"
// . you may add stats to this class, but do not remove them as they may
//   be necessary for Statsdb.

#ifndef _STATS_H_
#define _STATS_H_

#include "SafeBuf.h"
#include "UdpProtocol.h" // MAX_MSG_TYPES
//#include "IndexReadInfo.h"

class StatPoint {
 public:
	int32_t        m_numBytes  ;
	int64_t   m_startTime ;
	int64_t   m_endTime   ;
	int         m_color     ;
	char        m_rdbId     ; 
	char        m_type      ;
};

#define MAX_POINTS 6000
#define MAX_WIDTH  6
//#define DY         1000              // pixels vertical
#define DY         500              // pixels vertical
#define DX         1000             // pixels across
#define DT         (10*1000)        // time window, 10 seconds
#define MAX_LINES  (DY / (MAX_WIDTH+1)) // leave free pixel above each line

#define STAT_GENERIC 0
#define STAT_QUERY   1
#define MAX_BUCKETS  16

class Stats {

 public:

	// just clear our points array when we're born
	Stats ( ) ;

	// we take lower 3 bytes of "color" for the rgb color of this line/pt
	void addStat_r ( int32_t        numBytes    , 
			 int64_t   startTime   ,
			 int64_t   endTime     ,
			 int32_t        color       = 0x00000000 , // black 
			 char        type        = STAT_GENERIC,
			 char       *fnName      = NULL);

	// . use -1 to see points from ALL times stored
	// . dumps a bar graph
	// . each bar represents a stat in time, from inception to completion
	// . useful for seeing possible sources of contention
	//void dumpGIF ( int64_t startTime = -1 , int64_t endTime = -1 );


	void printGraphInHtml ( SafeBuf &sb );

	// this graphs:
	// 1. stats per second
	// 2. avg time of completion
	// 3. st dev highway around avg
	void dumpPerSecGIF ( char      type       ,
			     int64_t start = -1 , 
			     int64_t end   = -1 );

	// store the points here
	StatPoint m_pts [ MAX_POINTS ];
	int32_t      m_next;

	// don't graph any points that start BEFORE this time because our
	// sampling is not accurate because we had to throw points away
	// due to the MAX_POINTS limit
	//int64_t m_minWindowStartTime;

	// . conglomerate scores
	// . # seeks is estimated since we don't know the disk fragmentation
	//int64_t m_numSeeks;
	// sum of these 2 should equal m_numSeeks
	//int64_t m_numReads;
	//int64_t m_numWrites;
	// total bytes read and written since server was started
	//int64_t m_bytesRead;
	//int64_t m_bytesWritten;
	void addPoint ( StatPoint **points    , 
			int32_t        *numPoints ,
			StatPoint   *p         ) ;



	//queries
	void logAvgQueryTime(int64_t startTime);
	void calcQueryStats();

	void clearMsgStats();

	int64_t m_startTime;
	int64_t m_upTime;
	int64_t m_lastQueryLogTime;
	int64_t m_queryTimes;
	float     m_avgQueriesPerSec;
	int32_t      m_numQueries;
	int32_t      m_numSuccess;
	int32_t      m_numFails;
	int32_t      m_totalNumQueries;
	int32_t      m_totalNumSuccess;
	int32_t      m_totalNumFails;
	float     m_avgQueryTime;
	float     m_successRate;
	//int64_t m_readSignals;
	//int64_t m_writeSignals;

	// set in BigFile.cpp
	int32_t      m_slowDiskReads;

	// when we have to close a socket because too many are open.. count it
	int32_t      m_closedSockets;

	// keep track of last 1000 urls spidered for reporting spider stats
	void addSpiderPoint ( int32_t errCode, bool isNew ) ;
	int32_t m_spiderSample;
	int32_t m_spiderErrors;
	int32_t m_spiderNew;
	int32_t m_spiderErrorsNew;
	int32_t m_errCodes[1000];
	char m_isSampleNew[1000];
	int64_t m_totalSpiderSuccessNew;
	int64_t m_totalSpiderErrorsNew;
	int64_t m_totalSpiderSuccessOld;
	int64_t m_totalSpiderErrorsOld;
	int64_t m_allErrorsNew[65536];
	int64_t m_allErrorsOld[65536];

	time_t m_uptimeStart;

	// Deduped stats
	int32_t m_msg3aRecallCnt;
	// when we have to re-requrest docid lists for each split, inc this one
	int32_t m_msg3aSlowRecalls;
	// when we just request more docids from the same tier
	int32_t m_msg3aFastRecalls;
	// how many resolutions did we get on each tier
	//int32_t      m_tierHits [MAX_TIERS];
	//int64_t m_tierTimes[MAX_TIERS];
	// how many searches did not get enough results?
	int32_t m_tier2Misses;
	// one count for each CR_* defined in Msg51.h
	int32_t m_filterStats[30];
	//int32_t m_totalDedupCand;
	//int32_t m_dedupedCand;
	//int32_t m_bannedDups;
	//int32_t m_bigHackDups;
	//int32_t m_summaryDups;
	//int32_t m_contentDups;
	//int32_t m_clusteredTier1;
	//int32_t m_clusteredTier2;
	//int32_t m_errored;
	int32_t m_msg3aRecalls[6];
	SafeBuf m_keyCols;
	//int32_t m_numTermsVsTier[14][MAX_TIERS];
	//int32_t m_termsVsTierExp[14][MAX_TIERS][7];

	// use m_start so we know what msg stats to clear with memset
	char      m_start;
	// and stats for how long to send a request or reply from
	// start to finish. the first "2" is the niceness, 0 or 1, and
	// the second "2" is 0 if sending a reply and 1 if sending a request.
	int64_t m_msgTotalOfSendTimes    [MAX_MSG_TYPES][2][2];
	int64_t m_msgTotalSent           [MAX_MSG_TYPES][2][2];
	int64_t m_msgTotalSentByTime     [MAX_MSG_TYPES][2][2][MAX_BUCKETS];
	// how long we wait after receiving the request until handler is called
	int64_t m_msgTotalOfQueuedTimes  [MAX_MSG_TYPES][2];
	int64_t m_msgTotalQueued         [MAX_MSG_TYPES][2];
	int64_t m_msgTotalQueuedByTime   [MAX_MSG_TYPES][2][MAX_BUCKETS];
	// msg stats, from UdpServer.cpp and UdpSlot.cpp. all times in
	// milliseconds. the second index is the niceness, 0 or 1. Buckets
	// are for breaking down times by the time range: 0-1ms, 2-3ms,
	// 4-7ms, 8-15ms, ...
	int64_t m_msgTotalOfHandlerTimes [MAX_MSG_TYPES][2];
	int64_t m_msgTotalHandlersCalled [MAX_MSG_TYPES][2];
	int64_t m_msgTotalHandlersByTime [MAX_MSG_TYPES][2][MAX_BUCKETS];

	int32_t m_packetsIn  [MAX_MSG_TYPES][2];
	int32_t m_packetsOut [MAX_MSG_TYPES][2];
	int32_t m_acksIn     [MAX_MSG_TYPES][2];
	int32_t m_acksOut    [MAX_MSG_TYPES][2];
	int32_t m_reroutes   [MAX_MSG_TYPES][2];
	int32_t m_errors     [MAX_MSG_TYPES][2];
	int32_t m_timeouts   [MAX_MSG_TYPES][2]; // specific error
	int32_t m_nomem      [MAX_MSG_TYPES][2]; // specific error
	int32_t m_dropped    [MAX_MSG_TYPES][2]; // dropped dgram
	int32_t m_cancelRead [MAX_MSG_TYPES][2]; // dropped dgram

	int32_t m_parsingInconsistencies;

	int32_t m_totalOverflows;

	// count ip and domain hammer for Msg13.cpp here
	//int32_t m_numBackoffs;

	// used by msg39
	int32_t m_recomputeCacheMissess;
	// if the msg3a advances to the next tier, of course, it will be
	// a cache miss, so don't count those, they are justified 
	// recomputeCacheMisses
	int32_t m_icacheTierJumps;

	// when SpiderLoop calls SpiderCache::getSpiderRec() how many times
	// does it actually get back a url to spider? and how many times does
	// it miss (not get a url to spider)?
	//int32_t m_spiderUrlsHit;
	//int32_t m_spiderUrlsMissed;

	int32_t m_compressedBytesIn;
	int32_t m_uncompressedBytesIn;


	// spider compression proxy stats set in Msg13.cpp
	int64_t m_compressAllDocs;
	int64_t m_compressAllBytesIn;
	int64_t m_compressAllBytesOut;

	int64_t m_compressMimeErrorDocs;
	int64_t m_compressMimeErrorBytesIn;
	int64_t m_compressMimeErrorBytesOut;
			
	int64_t m_compressUnchangedDocs;
	int64_t m_compressUnchangedBytesIn;
	int64_t m_compressUnchangedBytesOut;
	
	int64_t m_compressBadContentDocs;
	int64_t m_compressBadContentBytesIn;
	int64_t m_compressBadContentBytesOut;

	int64_t m_compressBadLangDocs;
	int64_t m_compressBadLangBytesIn;
	int64_t m_compressBadLangBytesOut;

	int64_t m_compressBadCharsetDocs;
	int64_t m_compressBadCharsetBytesIn;
	int64_t m_compressBadCharsetBytesOut;

	int64_t m_compressBadCTypeDocs;
	int64_t m_compressBadCTypeBytesIn;
	int64_t m_compressBadCTypeBytesOut;

	int64_t m_compressHasIframeDocs;
	int64_t m_compressHasIframeBytesIn;
	int64_t m_compressHasIframeBytesOut;

	int64_t m_compressPlainLinkDocs;
	int64_t m_compressPlainLinkBytesIn;
	int64_t m_compressPlainLinkBytesOut;
	
	int64_t m_compressEmptyLinkDocs;
	int64_t m_compressEmptyLinkBytesIn;
	int64_t m_compressEmptyLinkBytesOut;
	
	int64_t m_compressFullPageDocs;
	int64_t m_compressFullPageBytesIn;
	int64_t m_compressFullPageBytesOut;
	
	int64_t m_compressHasDateDocs;
	int64_t m_compressHasDateBytesIn;
	int64_t m_compressHasDateBytesOut;
	
	int64_t m_compressRobotsTxtDocs;
	int64_t m_compressRobotsTxtBytesIn;
	int64_t m_compressRobotsTxtBytesOut;
	
	int64_t m_compressUnknownTypeDocs;
	int64_t m_compressUnknownTypeBytesIn;
	int64_t m_compressUnknownTypeBytesOut;

	// use m_end so we know what msg stats to clear with memset
	char      m_end;

	//bool m_gotLock;
};

extern class Stats g_stats;

#endif
