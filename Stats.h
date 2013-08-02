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
#include "IndexReadInfo.h"

class StatPoint {
 public:
	long        m_numBytes  ;
	long long   m_startTime ;
	long long   m_endTime   ;
	int         m_color     ;
	char        m_rdbId     ; 
	char        m_type      ;
};

#define MAX_POINTS 6000
#define MAX_WIDTH  6
#define DY         900              // pixels vertical
#define DX         1000             // pixels across
#define DT         (20*1000)        // time window, 10 seconds
#define MAX_LINES  (DY / (MAX_WIDTH+1)) // leave free pixel above each line

#define STAT_GENERIC 0
#define STAT_QUERY   1
#define MAX_BUCKETS  16

class Stats {

 public:

	// just clear our points array when we're born
	Stats ( ) ;

	// we take lower 3 bytes of "color" for the rgb color of this line/pt
	void addStat_r ( long        numBytes    , 
			 long long   startTime   ,
			 long long   endTime     ,
			 long        color       = 0x00000000 , // black 
			 char        type        = STAT_GENERIC,
			 char       *fnName      = NULL);

	// . use -1 to see points from ALL times stored
	// . dumps a bar graph
	// . each bar represents a stat in time, from inception to completion
	// . useful for seeing possible sources of contention
	void dumpGIF ( long long startTime = -1 , long long endTime = -1 );

	// this graphs:
	// 1. stats per second
	// 2. avg time of completion
	// 3. st dev highway around avg
	void dumpPerSecGIF ( char      type       ,
			     long long start = -1 , 
			     long long end   = -1 );

	// store the points here
	StatPoint m_pts [ MAX_POINTS ];
	long      m_next;

	// don't graph any points that start BEFORE this time because our
	// sampling is not accurate because we had to throw points away
	// due to the MAX_POINTS limit
	//long long m_minWindowStartTime;

	// . conglomerate scores
	// . # seeks is estimated since we don't know the disk fragmentation
	//long long m_numSeeks;
	// sum of these 2 should equal m_numSeeks
	//long long m_numReads;
	//long long m_numWrites;
	// total bytes read and written since server was started
	//long long m_bytesRead;
	//long long m_bytesWritten;
	void addPoint ( StatPoint **points    , 
			long        *numPoints ,
			StatPoint   *p         ) ;



	//queries
	void logAvgQueryTime(long long startTime);
	void calcQueryStats();

	void clearMsgStats();

	long long m_startTime;
	long long m_upTime;
	long long m_lastQueryLogTime;
	long long m_queryTimes;
	float     m_avgQueriesPerSec;
	long      m_numQueries;
	long      m_numSuccess;
	long      m_numFails;
	long      m_totalNumQueries;
	long      m_totalNumSuccess;
	long      m_totalNumFails;
	float     m_avgQueryTime;
	float     m_successRate;

	// set in BigFile.cpp
	long      m_slowDiskReads;

	// when we have to close a socket because too many are open.. count it
	long      m_closedSockets;

	// keep track of last 1000 urls spidered for reporting spider stats
	void addSpiderPoint ( long errCode, bool isNew ) ;
	long m_spiderSample;
	long m_spiderErrors;
	long m_spiderNew;
	long m_spiderErrorsNew;
	long m_errCodes[1000];
	char m_isSampleNew[1000];
	long long m_totalSpiderSuccessNew;
	long long m_totalSpiderErrorsNew;
	long long m_totalSpiderSuccessOld;
	long long m_totalSpiderErrorsOld;
	long long m_allErrorsNew[65536];
	long long m_allErrorsOld[65536];

	time_t m_uptimeStart;

	// Deduped stats
	long m_msg3aRecallCnt;
	// when we have to re-requrest docid lists for each split, inc this one
	long m_msg3aSlowRecalls;
	// when we just request more docids from the same tier
	long m_msg3aFastRecalls;
	// how many resolutions did we get on each tier
	long      m_tierHits [MAX_TIERS];
	long long m_tierTimes[MAX_TIERS];
	// how many searches did not get enough results?
	long m_tier2Misses;
	// one count for each CR_* defined in Msg51.h
	long m_filterStats[30];
	//long m_totalDedupCand;
	//long m_dedupedCand;
	//long m_bannedDups;
	//long m_bigHackDups;
	//long m_summaryDups;
	//long m_contentDups;
	//long m_clusteredTier1;
	//long m_clusteredTier2;
	//long m_errored;
	long m_msg3aRecalls[6];
	SafeBuf m_keyCols;
	long m_numTermsVsTier[14][MAX_TIERS];
	long m_termsVsTierExp[14][MAX_TIERS][7];

	// use m_start so we know what msg stats to clear with memset
	char      m_start;
	// and stats for how long to send a request or reply from
	// start to finish. the first "2" is the niceness, 0 or 1, and
	// the second "2" is 0 if sending a reply and 1 if sending a request.
	long long m_msgTotalOfSendTimes    [MAX_MSG_TYPES][2][2];
	long long m_msgTotalSent           [MAX_MSG_TYPES][2][2];
	long long m_msgTotalSentByTime     [MAX_MSG_TYPES][2][2][MAX_BUCKETS];
	// how long we wait after receiving the request until handler is called
	long long m_msgTotalOfQueuedTimes  [MAX_MSG_TYPES][2];
	long long m_msgTotalQueued         [MAX_MSG_TYPES][2];
	long long m_msgTotalQueuedByTime   [MAX_MSG_TYPES][2][MAX_BUCKETS];
	// msg stats, from UdpServer.cpp and UdpSlot.cpp. all times in
	// milliseconds. the second index is the niceness, 0 or 1. Buckets
	// are for breaking down times by the time range: 0-1ms, 2-3ms,
	// 4-7ms, 8-15ms, ...
	long long m_msgTotalOfHandlerTimes [MAX_MSG_TYPES][2];
	long long m_msgTotalHandlersCalled [MAX_MSG_TYPES][2];
	long long m_msgTotalHandlersByTime [MAX_MSG_TYPES][2][MAX_BUCKETS];

	long m_packetsIn  [MAX_MSG_TYPES][2];
	long m_packetsOut [MAX_MSG_TYPES][2];
	long m_acksIn     [MAX_MSG_TYPES][2];
	long m_acksOut    [MAX_MSG_TYPES][2];
	long m_reroutes   [MAX_MSG_TYPES][2];
	long m_errors     [MAX_MSG_TYPES][2];
	long m_timeouts   [MAX_MSG_TYPES][2]; // specific error
	long m_nomem      [MAX_MSG_TYPES][2]; // specific error
	long m_dropped    [MAX_MSG_TYPES][2]; // dropped dgram
	long m_cancelRead [MAX_MSG_TYPES][2]; // dropped dgram

	long m_parsingInconsistencies;

	// count ip and domain hammer for Msg13.cpp here
	//long m_numBackoffs;

	// used by msg39
	long m_recomputeCacheMissess;
	// if the msg3a advances to the next tier, of course, it will be
	// a cache miss, so don't count those, they are justified 
	// recomputeCacheMisses
	long m_icacheTierJumps;

	// when SpiderLoop calls SpiderCache::getSpiderRec() how many times
	// does it actually get back a url to spider? and how many times does
	// it miss (not get a url to spider)?
	//long m_spiderUrlsHit;
	//long m_spiderUrlsMissed;

	long m_compressedBytesIn;
	long m_uncompressedBytesIn;


	// spider compression proxy stats set in Msg13.cpp
	long long m_compressAllDocs;
	long long m_compressAllBytesIn;
	long long m_compressAllBytesOut;

	long long m_compressMimeErrorDocs;
	long long m_compressMimeErrorBytesIn;
	long long m_compressMimeErrorBytesOut;
			
	long long m_compressUnchangedDocs;
	long long m_compressUnchangedBytesIn;
	long long m_compressUnchangedBytesOut;
	
	long long m_compressBadContentDocs;
	long long m_compressBadContentBytesIn;
	long long m_compressBadContentBytesOut;

	long long m_compressBadLangDocs;
	long long m_compressBadLangBytesIn;
	long long m_compressBadLangBytesOut;

	long long m_compressBadCharsetDocs;
	long long m_compressBadCharsetBytesIn;
	long long m_compressBadCharsetBytesOut;

	long long m_compressBadCTypeDocs;
	long long m_compressBadCTypeBytesIn;
	long long m_compressBadCTypeBytesOut;

	long long m_compressHasIframeDocs;
	long long m_compressHasIframeBytesIn;
	long long m_compressHasIframeBytesOut;

	long long m_compressPlainLinkDocs;
	long long m_compressPlainLinkBytesIn;
	long long m_compressPlainLinkBytesOut;
	
	long long m_compressEmptyLinkDocs;
	long long m_compressEmptyLinkBytesIn;
	long long m_compressEmptyLinkBytesOut;
	
	long long m_compressFullPageDocs;
	long long m_compressFullPageBytesIn;
	long long m_compressFullPageBytesOut;
	
	long long m_compressHasDateDocs;
	long long m_compressHasDateBytesIn;
	long long m_compressHasDateBytesOut;
	
	long long m_compressRobotsTxtDocs;
	long long m_compressRobotsTxtBytesIn;
	long long m_compressRobotsTxtBytesOut;
	
	long long m_compressUnknownTypeDocs;
	long long m_compressUnknownTypeBytesIn;
	long long m_compressUnknownTypeBytesOut;

	// use m_end so we know what msg stats to clear with memset
	char      m_end;

	//bool m_gotLock;
};

extern class Stats g_stats;

#endif
