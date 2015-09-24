#include <errno.h>
//#define X_DISPLAY_MISSING 1
//#include <plotter.h>
#include <math.h>
#include "Conf.h"
#include "PingServer.h"
#include "gb-include.h"
#include "Statsdb.h"
#include "hash.h"
#include "Parms.h"

Statsdb g_statsdb;

static void flushStatsWrapper ( int fd , void *state ) ;
class Label {
public:
	int32_t m_graphType;
	// . SCALED unit max!
	// . -1 indicates to auto determine the maximum y value for time window
	float m_absYMax;
	// for hashing or printing in the map key
	char *m_label;
	// . min resolution per pixel in the graph
	// . pixely1 - pixely2 >= m_minRes
	// . compared AFTER applying m_yscalar
	float m_minRes;
	// for printing the y tick mark labels using sprintf:
	char *m_format;
	// for scaling the y values when printing tick marks
	float m_yscalar;
	// use this color
	int32_t  m_color;
	char  *m_keyDesc;
	// graph hash
	uint32_t m_labelHash;
	uint32_t m_graphHash;
};

#define GRAPH_OPS             0
#define GRAPH_QUANTITY        1
#define GRAPH_LATENCY         2
#define GRAPH_RATE            3
#define GRAPH_QUANTITY_PER_OP 4

static Label s_labels[] = {
	// . 30 is the max qps regardless to stop graph shrinkage
	// . use .03 qps as the min resolution per pixel
	// . changed 30 to 150 and .005 to .020
	// . back down to 30 since scale was too high before i think for most
	//   ppl. really it should be autoscaled.
	{ GRAPH_OPS,30,"query",.025,"%.1f qps",1.0,0x753d30,"queries per sec"},

	// . 10000 ms is the max latency to stop graph shrinkage
	// . use 1 ms as the min resolution per pixel
	// . use scalar of 1000 since stored in seconds, and we want ms
	{ GRAPH_LATENCY,5000,"query" , 1,"%.0f ms", 1000.0,  0x00b58869,"query latency"},

	// . quantity of query stats is the number of query terms
	// . 10000 ms is the max latency to stop graph shrinkage
	// . use 1 ms as the min resolution per pixel
	// . use scalar of 1000 since stored in seconds, and we want ms
	{ GRAPH_QUANTITY_PER_OP,10,"query" , .001,"%.0f terms", 1.0,  0x00ffffff,"terms per query"},


	// . 300MB/s is max read rate regardless to stop graph shrinkage
	// . use 1KB as the min resolution per pixel
	// . stored in Bps so use 1/1000 as scalar to get into KBps
	{ GRAPH_QUANTITY,200,"disk_read",1,"%.0f MBps",1.0/(1000.0*1000.0),0x000000,"disk read"},

	// . 300MB/s is max write rate regardless to stop graph shrinkage
	// . use 1KB as the min resolution per pixel
	// . stored in Bps so use 1/1000 as scalar to get into KBps
	{GRAPH_QUANTITY,200,"disk_write",1,"%.0f Mbps",1.0/(1000.0*1000.0), 0xff0000,	"disk write"},

	// . 20 is the max dps regardless to stop graph shrinkage
	// . use .03 qps as the min resolution per pixel
	{GRAPH_OPS,20,"parse_doc", .005,"%.1f dps" , 1.0 , 0x00fea915,"parsed doc" },


	{GRAPH_QUANTITY_PER_OP,-1,"docs_per_second", .1,"%.1f docs per second" , -1 , 0x1F2F5C,"*successfully* indexed docs per second" },

	// . use .1 * 1000 docs as the min resolution per pixel
	// . max = -1, means dynamic size the ymax!
	// . use 1B for now again...
	// . color=pink
	// . make it 2M now not 50M. seems like it is per pixel and theres
	//   like 1000 pixels vertically. but we need to autoscale it 
	//   eventually
	{GRAPH_QUANTITY,-1,"docs_indexed", .1,"%.0f docs" , -1,  0x00cc0099,"docs indexed" }


	//{ "termlist_intersect",0x0000ff00},
	//{ "termlist_intersect_soft",0x00008000}, // rat=0
	//{ "transmit_data_nice",0x00aa00aa },
	//{ "transmit_data", 0x00ff00ff },
	//{ "zak_ref_1a", 0x00ccffcc },
	//{ "zak_ref_1b",0x00fffacd },
	//{ "get_summary", 0x0000ff},
	//{ "get_summary_nice", 0x0000b0},
	//{ "get_gigabits",0x00d1e1ff },
	//{ "get_termlists_nice", 0x00aaaa00},
	//{ "get_termlists",0x00ffff00 },
	//{ "get_all_summaries", 0x008220ff},
	//{ "rdb_list_merge",0x0000ffff },
	//{ "titlerec_compress",0x00ffffff },
	//{ "titlerec_uncompress", 0x00ffffff} ,
	//{ "parm_change",0xffc0c0} // pink?
};

void drawLine3 ( SafeBuf &sb ,
		 int32_t x1 , 
		 int32_t x2 ,
		 int32_t fy1 , 
		 int32_t color ,
		 int32_t width ) ;

Label *Statsdb::getLabel ( int32_t labelHash ) {
	Label **label = (Label **)m_labelTable.getValue ( &labelHash );
	if ( ! label ) return NULL;
	return *label;
}


Statsdb::Statsdb ( ) {
	m_init = false;
	m_disabled = true;
}

bool Statsdb::init ( ) {

	// 20 pixel borders
	m_bx = 10;
	m_by = 40;

	// keep it at least at 20MB otherwise it is filling up the tree 
	// constantly and dumping
	int32_t maxTreeMem = g_conf.m_statsdbMaxTreeMem;
	if ( maxTreeMem < 10000000 ) maxTreeMem = 10000000;

	int32_t nodeSize = sizeof(StatData)+8+12+4 + sizeof(collnum_t);
	// for debug
	//nodeSize = 50000;

	// . We take a snapshot of g_stats every minute.
	// . Each sample struct taken from g_stats ranges from 1k - 2k
	//   after compression depending on the state of the
	//   all errors arrays.
	uint32_t maxTreeNodes  = maxTreeMem / nodeSize;
	uint32_t maxCacheNodes = g_conf.m_statsdbMaxCacheMem / nodeSize;

	// assume low niceness
	m_niceness = 0;

	// init the label table
	static char s_buf[832];
	if ( ! m_labelTable.set(4,sizeof(Label *),64,
				s_buf,832,false,0,"statcolors") )
		return false;
	// stock the table
	int32_t n = (int32_t)sizeof(s_labels)/ sizeof(Label);
	for ( int32_t i = 0 ; i < n ; i++ ) {
		Label *bb = &s_labels[i];
		// hash the label
		bb->m_labelHash = hash32n ( bb->m_label );
		// then incorporate the bool parm
		bb->m_graphHash = hash32h( bb->m_labelHash , bb->m_graphType );
		// add it to labeltable... why???
		if ( ! m_labelTable.addKey (&bb->m_graphHash,&bb ) ) { 
			char *xx=NULL;*xx=0; }
	}

	// sanity test
	//Stat ts;
	//ts.setKey ( 0x123456789LL , 0x7654321 );
	//if ( ts.getTime1()     != 0x123456789LL ) { char *xx=NULL;*xx=0; }
	//if ( ts.getLabelHash() != 0x7654321     ) { char *xx=NULL;*xx=0; }
	//ts.setKey ( 1268261684329LL , -246356284 );
	//if ( ts.getTime1()     != 1268261684329LL ) { char *xx=NULL;*xx=0; }
	//if ( ts.getLabelHash() != -246356284      ) { char *xx=NULL;*xx=0; }

	// call this twice per second
	if ( ! g_loop.registerSleepCallback(500,NULL,flushStatsWrapper))
		return log("statsdb: Failed to initialize timer callback2.");

	m_init   = true;

	// make the rec cache 0 bytes, cuz we are just using page cache now
	if ( ! m_rdb.init ( g_hostdb.m_dir		, // working directory
			    "statsdb"			, // dbname
			    true			, // dedup keys
			    sizeof(StatData)            , // fixed record size
			    200,//g_conf.m_statsdbMinFilesToMerge ,
			    maxTreeMem                  ,
			    maxTreeNodes		,
			    true			, // balance tree?
			    0                           , // m_statsdbMaxCchMem
			    maxCacheNodes		,
			    false			, // use half keys?
			    false			, // cache from disk?
			    NULL			, // page cache pointer
			    false			, // is titledb?
			    false			,
			    sizeof(key96_t)		, // key size
			    false, // bias disk page cache?
			    true ) ) // is collectionless?
		return false;

	m_disabled = false;

	// normally Collectiondb.addColl() will call Rdb::addColl() which
	// will init the CollectionRec::m_rdbBase, which is what
	// Rdb::getBase(collnum_t) will return. however, for collectionless
	// rdb databases we set Rdb::m_collectionlessBase special here.
	return m_rdb.addRdbBase1 ( NULL );
}



// Make sure we need this function.
// main.cpp currently uses the addColl from m_rdb
//bool Statsdb::addColl ( char *coll, bool doVerify ) {
//	if ( ! m_rdb.addColl ( coll ) ) return false;
//	return true;
//}

void flushStatsWrapper ( int fd , void *state ) {
	g_statsdb.addDocsIndexed();

	// force a statsdb tree dump if running out of room
	Rdb     *rdb  = &g_statsdb.m_rdb;
	RdbTree *tree = &rdb->m_tree;
	// if we got 20% room left and 50k available mem, do not dump
	if ( (float)tree->getNumUsedNodes() * 1.2 < 
	     (float)tree->getNumAvailNodes () &&
	     //tree->getNumAvailNodes () > 1000 &&
	     rdb-> m_mem.getAvailMem() > 50000 )
		return;

	if ( ! isClockInSync() ) return;

	// force a dump
	rdb->dumpTree ( 1 );
}

void Statsdb::addDocsIndexed ( ) {

	if ( ! isClockInSync() ) return;
	if ( g_hostdb.hasDeadHost() ) return;


	// only host #0 needs this
	if ( g_hostdb.m_hostId != 0 ) return;

	// only once per five seconds
	int32_t now = getTimeLocal();
	static int32_t s_lastTime = 0;
	if ( now - s_lastTime < 5 ) return;
	int32_t interval = now - s_lastTime;
	s_lastTime = now;

	int64_t total = 0LL;
	static int64_t s_lastTotal = 0LL;
	// every 5 seconds update docs indexed count
	for ( int32_t i = 0 ; i < g_hostdb.m_numHosts ; i++ ) {
		Host *h = &g_hostdb.m_hosts[i];
		// must have something
		if ( h->m_pingInfo.m_totalDocsIndexed <= 0 ) continue;
		// add it up
		total += h->m_pingInfo.m_totalDocsIndexed;
	}
	// divide by # of groups
	total /= g_hostdb.getNumHostsPerShard();
	// skip if no change

	if ( total == s_lastTotal ) return;

    int32_t docsIndexedInInterval = total - s_lastTotal;
    float docsPerSecond = docsIndexedInInterval / (float)interval;

	log("build: total docs indexed: %f. docs per second %f %i %i", (float)total, docsPerSecond, docsIndexedInInterval, interval);

	// add it if changed though
	int64_t nowms = gettimeofdayInMillisecondsGlobal();
	addStat ( MAX_NICENESS,"docs_indexed", nowms, nowms, (float)total );
    // Prevent a datapoint which adds all of the docs indexed to date.
    if( s_lastTotal != 0 ) {
        addStat ( MAX_NICENESS,"docs_per_second", nowms, nowms, docsPerSecond );
    }

	s_lastTotal = total;
}

// . m_key bitmap in statsdb:
//   tttttttt tttttttt tttttttt tttttttt  t = time in milliseconds, t1
//   tttttttt tttttttt tttttttt tttttttt
//   hhhhhhhh hhhhhhhh hhhhhhhh hhhhhhhh  h = hash32 of m_title
// . returns false if could not add stat, true otherwise
// . do not set g_errno if we return false just to keep things simple
// . we only add the stat to our local statsdb rdb, but because
//   we might be dumping statsdb to disk or something it is possible
//   we get an ETRYAGAIN error, so we try to accumulate stats in a
//   local buffer in that case
// . "label" is something like "queryLatency" or whatever
// . [t1,t2] are the time endpoints for the operation being measured
// . "value" is usually "numBytes", or a quantity indicator of whatever
//   was processed.
// . oldVal, newVal are reflect a state change, like maybe changing the
//   value of a parm. typically for such things t1 equals t2
bool Statsdb::addStat ( int32_t        niceness ,
			char      *label    ,
			int64_t   t1Arg    ,
			int64_t   t2Arg    ,
			float     value    , // y-value really, "numBytes"
			int32_t   parmHash ,
			float     oldVal   ,
			float     newVal   ,
			int32_t   userId32 ) {

	if ( ! g_conf.m_useStatsdb ) return true;

	// so Process.cpp can turn it off when dumping core
	if ( m_disabled ) return true;

	// not thread safe!
	//if ( g_threads.amThread() ) { 
	//	log("statsdb: called from thread");
	//	char *xx=NULL;*xx=0; 
	//}

	// . for now we can only add stats if we are synced with host #0 clock
	// . this is kinda a hack and it would be nice to not miss stats!
	if ( ! isClockInSync() ) return true;

	RdbTree *tree = &m_rdb.m_tree;
	// do not add stats to our tree if it is loading
	if ( tree->m_isLoading ) return true;

	// convert into host #0 synced time
	t1Arg = localToGlobalTimeMilliseconds ( t1Arg );
	t2Arg = localToGlobalTimeMilliseconds ( t2Arg );

	// sanity check
	if ( ! label ) { char *xx=NULL;*xx=0; }

	int32_t labelHash;
	if ( parmHash ) labelHash = parmHash;
	else            labelHash = hash32n ( label );

	// fix it for parm changes, and docs_indexed stat, etc.
	if ( t1Arg == t2Arg ) t2Arg++;

	// how many SECONDS did the op take? (convert from ms to secs)
	float dtms   = (t2Arg - t1Arg);
	float dtSecs = dtms / 1000.0;

	// we have already flushed stats 30+ seconds old, so if this op took
	// 30 seconds, discard it!
	if ( dtSecs >= 30 ) {
		//log("statsdb: stat is %"INT32" secs > 30 secs old, discarding.",
		//   (int32_t)dtSecs);
		return true;
	}

	int64_t nextup;

	// loop over all "second" buckets
	for ( int64_t tx = t1Arg ; tx < t2Arg ; tx = nextup ) {
		// get next second-aligned point in milliseconds
		nextup = ((tx +1000)/ 1000) * 1000;
		// truncate if we need to
		if ( nextup > t2Arg ) nextup = t2Arg;
		// . how much of the stat is in this time interval?
		// . like if operation took 3 seconds, we might cover
		//   50% of the first 1-second interval. so we use this
		//   as a weight for the stats we keep for that particular
		//   second. then we can plot a point for each second
		//   in time which is an average of all the queries that
		//   were in progress at that second.
		float fractionTime = ((float)(nextup - tx)) / dtms;

		// . get the time point bucket in which this stat belongs
		// . every "second" in time has a bucket
		uint32_t t1 = tx / 1000;

		StatKey sk;
		sk.m_zero      = 0x01; // make it a positive key
		sk.m_time1     = t1;
		sk.m_labelHash = labelHash;

		// so we can show just the stats for a particular user...
		if ( userId32 ) {
			sk.m_zero = userId32;
			// make it positive
			sk.m_zero |= 0x01; 
		}

		// if we already have added a bucket for this "second" then
		// get it from the tree so we can add to its accumulated stats.
		int32_t node1 = tree->getNode ( 0 , (char *)&sk );
		int32_t node2;

		StatData *sd;

		// get that stat, see if we are accumulating it already
		if ( node1 >= 0 ) 
			sd = (StatData *)tree->getData ( node1 );

		// make a new one if not there
		else {
			StatData tmp;
			// init it
			tmp.m_totalOps      = 0.0;
			tmp.m_totalQuantity = 0.0;
			tmp.m_totalTime     = 0.0;

			// save this
			int32_t saved = g_errno;
			// need to add using rdb so it can gbmemcpy the data
			if ( ! m_rdb.addRecord ( (collnum_t)0 ,
						 (char *)&sk,
						 (char *)&tmp,
						 sizeof(StatData),
						 niceness ) ) {
				if ( g_errno != ETRYAGAIN )
				log("statsdb: add rec failed: %s",
				    mstrerror(g_errno));
				// caller does not care about g_errno
				g_errno = saved;
				return false;
			}
			// caller does not care about g_errno
			g_errno = saved;
			// get the node in the tree
			//sd = (StatData *)tree->getData ( node1 );
			// must be there!
			node2 = tree->getNode ( 0 , (char *)&sk );
			// must be there!
			if ( node2 < 0 ) { char *xx=NULL;*xx=0; }
			// point to it
			sd = (StatData *)tree->getData ( node2 );
		}

		// use the milliseconds elapsed as the value if none given
		//if ( value == 0 && ! parmHash )
		//	value = t2Arg - t1Arg;

		// if we got it for this time, accumulate it
		// convert x into pixel position
		sd->m_totalOps      += 1      * fractionTime;
		sd->m_totalQuantity += value  * fractionTime;
		sd->m_totalTime     += dtSecs * fractionTime;
		
		if ( ! parmHash ) continue;

		sd->m_totalOps = 0;
		sd->m_totalQuantity = oldVal;
		sd->m_newVal        = newVal;
		// no fractions for this!
		break;
	}

	//logf(LOG_DEBUG,"statsdb: sp=0x%"XINT32"",(int32_t)sp);

	return true;
}	

#define MAXSAMPLES 1000

// . returns false if blocked, true otherwise
// . returns true and sets g_errno on error
bool Statsdb::makeGIF ( int32_t t1Arg , 
			int32_t t2Arg ,
			int32_t samples ,
			SafeBuf *sb2 ,
			void *state ,
			void (* callback) (void *state) ,
			int32_t userId32 ) {

	if ( t1Arg >= t2Arg ) {
		g_errno = EBADENGINEER;
		return true;
	}

	if ( t1Arg < 0 ) { char *xx=NULL;*xx=0; }
	if ( t2Arg < 0 ) { char *xx=NULL;*xx=0; }

	// # of samples in moving average
	m_samples  = samples;
	if ( m_samples > MAXSAMPLES ) m_samples = MAXSAMPLES;
	if ( m_samples < 1          ) m_samples = 1;

	m_state    = state;
	m_callback = callback;

	m_t1 = t1Arg;//(int64_t)t1Arg * 1000LL;
	m_t2 = t2Arg;//(int64_t)t2Arg * 1000LL;

	if ( m_t1 >= m_t2 ) { char *xx=NULL;*xx=0; }

	m_ht0.reset();
	m_sb0.reset();

	m_sb1.reset();

	m_sb2 = sb2;
	m_sb2->reset();

	m_sb3.reset();
	m_ht3.reset();

	// print graph in here as a bunch of divs now:
	m_gw.purge();
	m_dupTable.reset();
	m_dupTable.set(4,0,20000,NULL,0,false,0,"statstbl");

	// . start at t1 and get stats lists, up to 1MB of stats at a time
	// . subtract 60 seconds so we can have a better shot at having
	//   a moving average for the last SAMPLE points
	m_startKey.n1 = m_t1 - 60;
	if ( m_startKey.n1 < 0 ) m_startKey.n1 = 0;
	m_startKey.n0 = 0x0000000000000000LL;
	m_endKey.n1   = m_t2;
	m_endKey.n0   = 0xffffffffffffffffLL;

	m_done = false;

	if ( ! m_ht0.set ( 4 , 4,256,NULL,0,true,m_niceness,"statht0") ) 
		return true;

	if ( ! m_ht3.set ( 4 , 4,256,NULL,0,true,m_niceness,"statht3") ) 
		return true;

	// open the file for the gif
	/*
	char fname [ 1024 ];
	sprintf ( fname , "%s/stats%"INT32".gif" ,
		  g_hostdb.m_httpRootDir , g_hostdb.m_hostId );
	m_fd = fopen ( fname,"w" );
	if ( ! m_fd ) { 
		log("admin: Statsdb::makeGIF: fopen ( %s , \"w\" ) : %s", 
		    fname , mstrerror(errno) );
		return true;
	}
	*/

	return gifLoop ();
}

#define POINTWIDTH 8

#define MAX_POINTS 6000
#define MAX_WIDTH  6
//#define DY2        600              // pixels vertical
#define DY2        400              // pixels vertical
#define DX2        1000             // pixels across
#define MAX_LINES2  (DY2 / (MAX_WIDTH+1)) // leave free pixel above each line

int32_t Statsdb::getImgHeight() {
	return (int32_t)DY2 + m_by * 2;
}

int32_t Statsdb::getImgWidth() {
	return (int32_t)DX2 + m_bx * 2;
}

// these are used for storing the "events"
class EventPoint {
public:
	// m_a and m_b are in pixel space
	int32_t  m_a;
	int32_t  m_b;
	int32_t  m_parmHash;
	float m_oldVal;
	float m_newVal;
	//int32_t  m_colorRGB;
	int32_t  m_thickness;
};

static void gotListWrapper ( void *state , RdbList *list, Msg5 *msg5 ) ;

// returns false if blocked, true otherwise
bool Statsdb::gifLoop ( ) {
	// int16_tcut
	Msg5 *m = &m_msg5;

	//#ifndef _USEPLOTTER_
	//return true;
	//#endif

	// loop over all the lists in the time range, [m_t1,m_t2]
	for ( ; ! m_done ; ) {
		if ( ! m->getList ( (char)RDB_STATSDB	,
				    (collnum_t)0,//"statsdb" , // coll
				    &m_list		,
				    (char *)&m_startKey	,
				    (char *)&m_endKey	,
				    32000	, // requested scan size
				    true 	, // include tree?
				    false	, // add to cache?
				    0		, // max cache age
				    0		, // start file number
				    -1		, // number of files
				    NULL	, // state
				    gotListWrapper, // callback
				    m_niceness	, // niceness
				    false	, // do error correction?
				    NULL	, // cache key pointer
				    0		, // # retries
				    -1		, // max # retries
				    true	, // compensate for merge?
				    -1		, // sync point
				    NULL	) ) // msg5b
			return false;
		// . process list
		// . returns false with g_errno set on error
		if ( ! processList() ) return true;
	}

	// define time delta - commented out because it's currently not used.
	int32_t dt = m_t2 - m_t1;

	//#ifdef _USEPLOTTER_

	// gif size
	//char tmp[64];
	// dimensions of the gif
	//sprintf ( tmp , "%"INT32"x%"INT32"", (int32_t)DX2+m_bx*2 , (int32_t)DY2+m_by*2 );
	//GIFPlotter::parampl ( "BITMAPSIZE" , (void *)tmp );
	// create one
	//GIFPlotter plotter ( NULL , m_fd , NULL );
	// open it
	//plotter.openpl ( );

	// define the space with boundaries 100 unit wide boundaries
	//plotter.space ( 0 , 0 , DX2 + m_bx * 2 , DY2 + m_by * 2 );

	// line thickness in user coordinates (pixels for us)
	//plotter.linewidth ( 1 );       
	// set bg color to gray (r/g/b) 
	//plotter.bgcolor ( 0xd600 , 0xce00 , 0xd600 );
	// erase Plotter's graphics display
	//plotter.erase ();                
	// draw axises in black
	//plotter.pencolorname ("black");    

	//
	// main graphing window
	//
	m_gw.safePrintf("<div style=\"position:relative;"
					"background-color:#c0c0c0;"
					//"overflow-y:hidden;"
					"overflow-x:hidden;"
					"z-index:0;"
					// the tick marks we print below are based on it
					// being a window of the last 20 seconds... and using
					// DX2 pixels
					"min-width:%"INT32"px;"
					"min-height:%"INT32"px;"
					//"width:100%%;"
					//"min-height:600px;"
					"margin-top:10px;"
					"margin-bottom:10px;"
					"margin-right:10px;"
					"margin-left:10px;\">"
					,(int32_t)DX2 + 2 *m_bx
					,(int32_t)DY2 + 2*m_by);


	// draw the x-axis
	//plotter.line ( m_bx , m_by , DX2 + m_bx , m_by  );

	// 10 x-axis tick marks
	for ( int x = DX2/20 ; x <= DX2 ; x += DX2/20 ) {
		// tick mark
		//plotter.line ( x , -20 , x , 20 );
		m_gw.safePrintf("<div style=\"position:absolute;"
			      "left:%"INT32";"
			      "bottom:0;"
			      "background-color:black;"
			      "z-index:10;"
			      "min-height:20px;"
			      "min-width:3px;\"></div>\n"
			      , m_bx + (int32_t)x-1
			      );
		int32_t xv = (int32_t)(dt * (int64_t)x/(int64_t)DX2)-(int32_t)dt;
		// LABEL
		m_gw.safePrintf("<div style=\"position:absolute;"
				"left:%"INT32";"
				"bottom:22;"
				//"background-color:#000000;"
				"z-index:10;"
				"height:20px;"
				"width:3px;"
				"color:black;"
				"font-size:10px;"
				"min-height:20px;"
				"min-width:3px;\">%"INT32"s</div>\n"
				, (int32_t)x-15 + m_bx
				// the label:
				, xv
				);
	}


	HashTableX tmpht;
	tmpht.set(4,0,0,NULL,0,false,m_niceness,"statsparms");

	int32_t col = 0;

	m_sb2->safePrintf("<table border=1 width=100%%>\n");

	// label offset to prevent collisions of superimposing multiple
	// graph calbrations
	int32_t zoff = 0;


	//
	// point to the triplets in m_sb1's buffer (x,y,c)
	//
	char *p    = m_sb1.getBufStart();
	char *pend = p + m_sb1.length();
	for ( ; p < pend ; p += 12 ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get graph hash of this point
		int32_t  gh = *(int32_t *)(p +8);

		// if we already did this graph, skip it
		if ( tmpht.isInTable ( &gh ) ) continue;

		// . graph this single graph of this color
		// . returns ptr to first point of different color!
		plotGraph ( p , pend , gh , m_gw , zoff );
		// prevent collisions
		zoff += 20;

		// get the label based on graphHash
		Label *bb = getLabel ( gh );

		// add to key
		if ( col == 0 )
			m_sb2->safePrintf("<tr>");

		m_sb2->safePrintf("<td bgcolor=#%06"XINT32"><label>"
						  "<input class=\"graph-toggles\" "
						  "type=\"checkbox\" "
						  "value=\"%06"XINT32"\"/></label></td>"
						  "<td>%s</td>\n",
						  bb->m_color ,
						  bb->m_color ,
						  bb->m_keyDesc );

		if ( col == 1 )
			m_sb2->safePrintf("</tr>\n");

		// inc column and wrap
		if ( ++col >= 2 ) col = 0;

		// . do not re-display 
		// . TODO: deal with error
		tmpht.addKey ( &gh );
		
	}



	// clear that up
	m_sb1.reset();

	// now plot the events, horizontal line segments like the performance
	// graph uses
	for ( int32_t i = 0 ; i < m_ht3.m_numSlots ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// skip if slot empty
		if ( ! m_ht3.m_flags[i] ) continue;
		// get the offset into m_sb3
		int32_t offset = *(int32_t *)m_ht3.getValueFromSlot(i);
		// get buf start
		char *bufStart = m_sb3.getBufStart();
		// get the ptr
		EventPoint *pp = (EventPoint *)(bufStart + offset);

		// get name of parm
		Parm *m = g_parms.getParmFromParmHash ( pp->m_parmHash );
		// make sure we got it
		if ( ! m ) { 
			log("statsdb: unrecognized parm hash = %"INT32"",
			    pp->m_parmHash);
			continue;
			//char *xx=NULL;*xx=0; }
		}

		// set the line width
		//plotter.linewidth ( pp->m_thickness );

		// get parm hash
		int32_t colorHash = pp->m_parmHash;
		// add in old/new values to make it different
		colorHash = hash32h ( (int32_t)pp->m_oldVal , colorHash );
		colorHash = hash32h ( (int32_t)pp->m_newVal , colorHash );
		// . get color
		// . is really the parm hash in disguise
		int32_t c1 = colorHash & 0x00ffffff;
		// use the color specified from addStat_r() for this line/pt
		//plotter.pencolor ( ((c1 >> 16) & 0xff) << 8 ,
		//		   ((c1 >>  8) & 0xff) << 8 ,
		//		   ((c1 >>  0) & 0xff) << 8 );

		int32_t x1 = pp->m_a;
		int32_t x2 = pp->m_b;
		int32_t y1 = *(int32_t *)m_ht3.getKey(i); // i value
		// ensure at least 3 units wide for visibility
		if ( x2 < x1 + 10 ) x2 = x1 + 10;
		// . flip the y so we don't have to scroll the browser down
		// . DY2 does not include the axis and tick marks
		//int32_t fy1 = DY2 - y1 + m_by ;
		// plot it
		//plotter.line ( x1 , fy1 , x2 , fy1 );
		drawLine3 ( m_gw , x1 , x2 , y1 , c1 , pp->m_thickness );

		// add to map key? only if we haven't already
		if ( tmpht.isInTable ( &colorHash ) ) continue;

		// add it
		if ( col == 0 )
			m_sb2->safePrintf("<tr>");

		char *title = "unknown parm";
		if ( m ) title = m->m_title;

		m_sb2->safePrintf("<td bgcolor=#%06"XINT32">&nbsp; &nbsp;</td>",c1);

		// print the parm name and old/new values
		m_sb2->safePrintf("<td><b>%s</b>",title);

		if ( pp->m_oldVal != pp->m_newVal )
			m_sb2->safePrintf(" (%.02f -> %.02f)",
					 pp->m_oldVal,pp->m_newVal);

		m_sb2->safePrintf("</td>");

		if ( col == 1 )
			m_sb2->safePrintf("</tr>\n");

		// inc column and wrap
		if ( ++col >= 2 ) col = 0;

		// . do not re-display 
		// . TODO: deal with error
		tmpht.addKey ( &colorHash ) ;
	}
	m_sb2->safePrintf("</table>\n");

	// clear that up
	m_ht3.reset();
	m_sb3.reset();

	// and stat states
	m_ht0.reset();
	m_sb0.reset();

	// all done free some mem
	m_sb1.reset();
	//m_sb2.reset();

	//
	// but not m_sb2 cuz that has the html in it!!
	//

	// all done
	//if ( plotter.closepl () < 0 ) 
	//	log("admin: Could not close performance graph object.");
	// close the file
	//fclose ( m_fd );

	//#endif

	// close main graphing window
	m_gw.safePrintf("</div>\n");

	return true;
}


char *Statsdb::plotGraph ( char *pstart , 
			   char *pend , 
			   int32_t graphHash , 
			   //GIFPlotter *plotter ,
			   SafeBuf &gw ,
			   int32_t zoff ) {

	// . use "graphHash" to map to unit display
	// . this is a disk read volume
	Label *label = getLabel ( graphHash );
	if ( ! label ) { char *xx=NULL;*xx=0; }

	//log("stats: plotting %s",label->m_keyDesc) ;

	// let's first scan m_sb1 to normalize the y values
	bool needMin = true;
	bool needMax = true;
	float ymin = 0.0;
	float ymax = 0.0;
	float yscalar = label->m_yscalar;
	char *p = pstart;

	for ( ; p < pend ; p += 12 ) {
		// breathe
		QUICKPOLL ( m_niceness );
		if ( m_gw.getLength() > 10000000 ) break;
		// get the y
		float y2 = *(float *)(p+4);
		// get color of this point
		int32_t  gh = *(int32_t *)(p +8);
		// stop if not us
		if ( gh != graphHash ) continue;
		// put into scaled space right away
		if (label->m_yscalar >= 0)
			y2 = y2 * label->m_yscalar;
		// . limit y to absolute max
		// . these units should be scaled as well!
		if ( y2 > label->m_absYMax && label->m_absYMax > 0.0 )
			y2 = label->m_absYMax;
		// get min and max
		if ( y2 < ymin || needMin ) ymin = y2;
		if ( y2 > ymax || needMax ) ymax = y2;
		needMin = false;
		needMax = false;
	}

	// force to zero for now
	//ymin = 0.0;
	// . and force to ymax for now as well
	// . -1 indicates dynamic though!
	if ( label->m_absYMax > 0.0 ) ymax = label->m_absYMax;
	// add a 20% ceiling
	//	else                          ymax *= 1.20;

	
	if( label->m_yscalar <= 0 ) {
        if(ymax == ymin) {
            yscalar = 0;
        } else {
            yscalar = (float)DY2 / (ymax - ymin);
        }
	}
	// return that!
	char *retp = p;

	// set the line width
	//plotter->linewidth ( 1 );

	int32_t color = label->m_color;

	// use the color specified from addStat_r() for this line/pt
	//plotter->pencolor ( ((color >> 16) & 0xff) << 8 ,
	//		    ((color >>  8) & 0xff) << 8 ,
	//		    ((color >>  0) & 0xff) << 8 );


	// . the minimum difference between ymax and ymin is minDiff.
	// . this prevents us from zooming in too close!
	float minDiff = (float)DY2     * label->m_minRes ;
	// we are already scaled!
	float ourDiff = (ymax - ymin) ;

	// . pad y range if total range is small
	// . only do this for certain types of stats, like qps and disk i/o
	if ( label->m_yscalar >=0 && ourDiff < minDiff ) {
		float pad = (minDiff - ourDiff) / 2;
		// pad it out
		ymin -= pad ;
		ymax += pad ;
		// fix it some
		if ( ymin < 0 ) {
			ymax += -1*ymin;
			ymin  = 0;
		}
		// limit again just in case
		if ( ymax > label->m_absYMax && label->m_absYMax > 0.0 )
			ymax = label->m_absYMax;
	}		


	// set the line width
	//plotter->linewidth ( 2 );

	// reset for 2nd scan
	p = pstart;

	int32_t  lastx = -1;
	float lasty ;
	bool  firstPoint = true;

	// now the m_sb1 buffer consists of points to make lines with
	for ( ; p < pend ; ) {
		// breathe
		QUICKPOLL ( m_niceness );
		if ( m_gw.getLength() > 10000000 ) break;
		// first is x pixel pos
		int32_t  x2 = *(int32_t *)p; p += 4;
		// then y pos
		float y2 = *(float *)p; p += 4;

		// scale it right away
		if(label->m_yscalar < 0) {
			y2 = (y2 - ymin) * yscalar;
		}
		else {
			y2 *= yscalar;

		}
		// adjust
		if ( y2 > ymax ) y2 = ymax;
		if ( y2 < 0 ) y2 = 0;

		// then graphHash
		int32_t  gh = *(int32_t *)p; p += 4;

		// skip if wrong graph
		if ( gh != graphHash ) continue;

		// set first point for making the line
		int32_t  x1 = lastx;
		float y1 = lasty;

		// normalize y into pixel space
		if(label->m_yscalar >= 0 && ymax != ymin) {
			y2 = ((float)DY2 * (y2 - ymin)) / (ymax-ymin);
		}
		
		// set lasts for next iteration of this loop
		lastx = x2;
		lasty = y2;

		// . flip the y so we don't have to scroll the browser down
		// . DY2 does not include the axis and tick marks
		// . do not flip y any more for statsdb graphs
		int32_t fy1 = (int32_t)(y1+.5);// + m_by ;
		int32_t fy2 = (int32_t)(y2+.5);// + m_by ;

		// how are we getting -.469 for "query" point?
		if ( fy1 < 0 ) continue;
		if ( fy2 < 0 ) continue;

		// skip if can't make a line
		if ( firstPoint ) { 
			//plotter->circle ( x2 , fy2 , 2 );
			int32_t width = POINTWIDTH;
			// draw a 4x4 box now:
			drawLine3(m_gw,x2-width/2,x2+width/2,fy2,color,width); 
			firstPoint = false;
			continue;
		}

		// log it
		//logf(LOG_DEBUG,"plot: (%"INT32",%.02f) - (%"INT32",%.02f) [%s]",
		//     x1 , y1 , x2 , y2 , label->m_label );

		// ensure at least 3 units wide for visibility
		//if ( x2 < x1 + 10 ) x2 = x1 + 10;

		// plot it
		// BUT only iff not more than 5 seconds difference
		//float secondsPerPixel = (m_t2-m_t1)/(float)DX2;

		// avoid this for now. mdw oct 14 2013.
		//float dt = (x2 - x1) * secondsPerPixel;
		//if ( dt <= 13 || x2 - x1 <= 10 )
		//	plotter->line ( x1 , fy1 , x2  , fy2 );

		// circle second point
		//plotter->circle ( x1 , fy1 , 2 );
		//plotter->circle ( x2 , fy2 , 2 );
		// draw a 4x4 boxes now:
		int32_t width = POINTWIDTH;
		drawLine3 ( m_gw,x1-width/2, x1+width/2, fy1,color, width); 
		drawLine3 ( m_gw,x2-width/2, x2+width/2, fy2,color, width); 
	}

	//plotter->linewidth ( 1 );

	// plot unit lines
	float deltaz = (ymax-ymin) / 6;
	if ( strstr(label->m_keyDesc,"latency" ) ) {
		// draw it
		drawHR ( 400.0 - 111.0 , ymin,ymax,m_gw,label,zoff,0xff0000);
		drawHR ( 600.0-111.0,ymin,ymax,m_gw,label,zoff,color);
	}

	if ( strstr(label->m_keyDesc,"queries per sec" ) ) {
		// draw it
		//deltaz /= 2;
		//drawHR(120.0, ymin , ymax , plotter , label , zoff , color );
		//drawHR(130.0, ymin , ymax , plotter , label , zoff , color );
		drawHR ( 140.0 , ymin , ymax ,m_gw , label , zoff , color );
	}


	float lastZ = -1;
	for ( float z = ymin ; z < ymax ; z += deltaz ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// draw it
		drawHR ( z , ymin , ymax , m_gw , label , zoff , color );
		if(z == lastZ) break;
		lastZ = z;
		//if ( m_gw.getLength() > 10000000 ) break;
	}

	if ( m_gw.getLength() > 10000000 )
		log("statsdb: graph too big");

	return retp;
	//#endif
       
}

void Statsdb::drawHR ( float z ,
		       float ymin , 
		       float ymax ,
		       //GIFPlotter *plotter ,
		       SafeBuf &gw,
		       Label *label ,
		       float zoff ,
		       int32_t color ) {

	// convert into yspace
	float z2 = ((float)DY2 * (float)(z - ymin)) /(float)(ymax-ymin);
	// avoid collisions with other graphs
	z2 += zoff;
	// border
	//z2 += m_by;
	// round off error
	z2 += 0.5;
	// for adjusatmnet
	float ptsPerPixel = (ymax-ymin)/ (float)DY2;
	// make an adjustment to the label then! -- Commented out because it's currently not used.
	float zadj = zoff * ptsPerPixel;

	//#ifdef _USEPLOTTER_

	// use the color specified from addStat_r() for this line/pt
	//plotter->pencolor ( ((color >> 16) & 0xff) << 8 ,
	//		    ((color >>  8) & 0xff) << 8 ,
	//		    ((color >>  0) & 0xff) << 8 );

	// horizontal line
	//plotter->line ( m_bx, (int32_t)z2 , DX2 + m_bx, (int32_t)z2 );
	int32_t width = 1;
	drawLine3 ( m_gw, 0, DX2 , (int32_t)z2,color, width); 


	// make label
	char tmp[128];
	// . use "graphHash" to map to unit display
	// . this is a disk read volume
	sprintf(tmp,label->m_format,z +zadj);//* label->m_yscalar);

	/*
	// a white shadow
	plotter->pencolor ( 0xffff,0xffff,0xffff );
	plotter->move ( m_bx + 80 + 2 , z2 + 10 - 2 );
	plotter->alabel     ( 'c' , 'c' , tmp );
	
	// a black shadow
	plotter->pencolor ( 0 , 0 , 0 );
	plotter->move ( m_bx + 80 + 1 , z2 + 10 - 1 );
	plotter->alabel     ( 'c' , 'c' , tmp );
	
	//int32_t color = label->m_color;
	// use the color specified from addStat_r() for this line/pt
	plotter->pencolor ( ((color >> 16) & 0xff) << 8 ,
			    ((color >>  8) & 0xff) << 8 ,
			    ((color >>  0) & 0xff) << 8 );
	
	// move cursor
	plotter->move ( m_bx + 80 , z2 + 10 );
	// plot label
	plotter->alabel     ( 'c' , 'c' , tmp );
	*/

	// LABEL
	gw.safePrintf("<div style=\"position:absolute;"
				  "left:%"INT32";"
				  "bottom:%"INT32";"
				  "color:#%"XINT32";"
				  "z-index:110;"
				  "font-size:14px;"
				  "min-height:20px;"
				  "min-width:3px;\""
                  " class=\"color-%"XINT32"\""
				  ">%s</div>\n"
		      , (int32_t)(m_bx)
		      , (int32_t)z2 +m_by
		      , color
		      // the label:
		      , color
		      , tmp
		      );
	
}

void gotListWrapper ( void *state , RdbList *list, Msg5 *msg5 ) {
	// add in some graph points
	g_statsdb.processList();
	// do more, return if blocked
	if ( ! g_statsdb.gifLoop() ) return;
	// call callback
	g_statsdb.m_callback ( g_statsdb.m_state );
}

bool Statsdb::processList ( ) {

	if ( m_list.isEmpty() )
		m_done = true;

	else {
		// update start key for next disk read
		m_list.getLastKey ( (char *)&m_startKey );
		// add one
		m_startKey += 1;
		// done if wrapped
		if ( m_startKey.n0 == 0LL && m_startKey.n1 == 0 )
			m_done = true;
	}

	// HACK: the user can request all of the events, it can
	// become quite large. so limit to 100 mb right now.
	if( m_sb3.length() > 100000000) {
		log("statsdb: truncating statsdb results.");
		m_done = true;
	}


	//
	// all these points are accumulated into 1-second buckets
	//

	int32_t n = (int32_t)sizeof(s_labels)/ sizeof(Label);

	for ( int32_t i = 0 ; i < n ; i++ ) {
		// get the label we want to graph
		Label *bb = &s_labels[i];
		// add the points to m_sb1
		if ( ! addPointsFromList ( bb ) ) return false;
	}

	if ( ! addEventPointsFromList ( ) )
		return false;

	return true;
}

// so we do not have to rescan the same lists for every stat we graph
class StatState {
public:
	float m_ringBuf     [MAXSAMPLES];
	int32_t  m_ringBufTime [MAXSAMPLES];
	char  m_valid       [MAXSAMPLES];
	int32_t  m_numSamples ;
	float m_sumVal  ;
	int32_t  m_i           ;

	int32_t  m_lastx;
	float m_lasty;
	float m_lastWeight;

};

// this preserves a particular stats state when scanning multiple rdb lists
// so we do not have to rescan the same lists for every stat we graph
StatState *Statsdb::getStatState ( int32_t us ) {
	// get the offset
	int32_t *offsetPtr = (int32_t *)m_ht0.getValue ( &us );
	// if there, return it
	if ( offsetPtr ) {
		// sanity check
		if ( *offsetPtr <  0              ) { char *xx=NULL;*xx=0; }
		if ( *offsetPtr >= m_sb0.length() ) { char *xx=NULL;*xx=0; }
		// get buf start
		char *buf = m_sb0.getBufStart();
		// point to it
		StatState *ss = (StatState *)(buf + *offsetPtr);
		// return if there
		return ss;
	}
	// reserve
	if ( ! m_sb0.reserve2x ( sizeof(StatState  ) ) ) return NULL;
	// make it otherwise
	StatState *ss = (StatState *)m_sb0.getBuf();
	// store the offset
	int32_t offset = m_sb0.length();
	// skip that
	m_sb0.incrementLength ( sizeof(StatState) );
	// store that - return NULL with g_errno set on error
	if ( ! m_ht0.addKey ( &us , &offset ) ) return NULL;
	// reset its member vars
	memset  ( ss->m_valid , 0 , m_samples );
	ss->m_numSamples  = 0;
	ss->m_sumVal      = 0.0;
	ss->m_i           = 0;
	ss->m_lastx       = -1;
	// got it
	return ss;
}

// . return false with g_errno set on error, true otherwise
// . looking at the number of points per second
// . average query latency for last 20 queries
// . average disk bytes read for last 20 accesses
// . val is the State::m_value measurement, a float
// . also each point may represent a number of bytes transferred in which
//   case we use that number rather than "1", which is the default
bool Statsdb::addPointsFromList ( Label *label ) {

	StatState *ss  = getStatState ( label->m_graphHash );
	// return false with g_errno set
	if ( ! ss ) return false;

	m_list.resetListPtr();
	// scan the list for our junk
	for ( ; ! m_list.isExhausted() ; m_list.skipCurrentRecord() ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get that
		StatKey *sk = (StatKey *)m_list.getCurrentRec();
		// and data
		StatData *sd = (StatData *)m_list.getCurrentData();
		// must be a "query" stat
		if ( sk->m_labelHash != label->m_labelHash ) continue;
		// add this record directly from statsdb
		addPoint ( sk , sd , ss , label );
	}
	return true;
}

bool Statsdb::addPoint(StatKey *sk,StatData *sd,StatState *ss , Label *label){
	 
	 // if measuring "queries per second" or "disk reads per sec"
	 // then compare our start time to start time of the operation
	 // before us.
	 float val;
	 if      ( label->m_graphType == GRAPH_QUANTITY )
		 val = sd->m_totalQuantity;
	 else if ( label->m_graphType == GRAPH_OPS )
		 val = sd->m_totalOps;
	 else if ( label->m_graphType == GRAPH_LATENCY )
		 val = sd->m_totalTime / sd->m_totalOps;
	 else if ( label->m_graphType == GRAPH_RATE )
		 val = sd->m_totalQuantity / sd->m_totalTime;
	 else if ( label->m_graphType == GRAPH_QUANTITY_PER_OP )
		 val = sd->m_totalQuantity / sd->m_totalOps;
	 else { char *xx=NULL;*xx=0; }

	 // remove tail. this ringbuffer is used to make the moving average.
	 int32_t k = ss->m_i;
	 for ( ; ss->m_valid[k] ; ) {
		 // remove from accumulated sum from moving avg calc
		 ss->m_sumVal -= ss->m_ringBuf[k];
		 // this too
		 ss->m_numSamples--;
		 // invalidate it
		 ss->m_valid[k] = false;
		 // stop if time within range
		 if ( ss->m_ringBufTime[k] >= sk->m_time1 - m_samples )
			 break;
		 // otherwise, keep removing samples from moving avg
		 // if outside of time range. wrap around when k hits
		 // m_samples. thus, the "ring" in "ring buffer"
		 if ( ++k >= m_samples ) k = 0;
	 }

	 // must not be valid!
	 if ( ss->m_valid[ss->m_i] ) { char *xx=NULL;*xx=0; }

	 // we are valid now
	 ss->m_valid[ss->m_i] = true; 

	 // set time stamp
	 ss->m_ringBufTime[ss->m_i] = sk->m_time1;

	 // set value
	 ss->m_ringBuf[ss->m_i] = val;
	 
	 // show the individual point
	 //if ( doLatency )
	 //	logf(LOG_DEBUG,"statsdb: (%"UINT64", %"UINT64") [%s]",
	 //	     sp->getTime1(),delta,label->m_label);
	 //else
	 //	logf(LOG_DEBUG,"statsdb: (%"UINT64", %.03f) [%s]",
	 //	     sp->getTime1(),quantity,label->m_label);
	 
	 // inc and wrap
	 if ( ++ss->m_i >= m_samples ) ss->m_i = 0;
	 
	 // add it up
	 ss->m_numSamples += 1;
	 // . quantity should always be one
	 ss->m_sumVal  += val;
	 
	 // do not destroy ourselves
	 if ( ss->m_numSamples > m_samples ) 
		 log("statsdb: bad engineer.");
	 
	 // . do not add it if before m_t1
	 // . we set m_startKey lower than m_t1 in order to get
	 //   a running start on our moving average algorithm
	 if ( sk->m_time1 < m_t1 ) return true;

	 // do not start showing points until we got 20 so we can
	 // make a decent average and avoid initial spikes!!!
	 //if ( ss->m_numSamples < m_samples ) // 5 )
	 //	continue;
	 
	 // . plot that point
	 // . should make a line between it and the last point added
	 //if(! addPoint ( sp->getTime1() , val , colorRGB , weight ) )

	 // now the moving average at time "sk->m_time1" is
	 // ss->m_sumVal / (float)ss->m_numSamples, so add that point to
	 // our graph.
	 if ( ! addPoint ( sk->m_time1 , 
			   ss->m_sumVal / (float)ss->m_numSamples ,
			   label->m_graphHash , 
			   1 , 
			   ss ) )
		 return false;

	return true;
}


bool Statsdb::addPoint ( int32_t      x        ,
			 float     y        ,
			 int32_t      graphHash ,
			 float     weight   ,
			 class     StatState *ss ) {

	// convert x into pixel position
	float xf = (float)DX2 * (float)(x - m_t1) / (float)(m_t2 - m_t1);
	// round it to nearest pixel
	int32_t  x2 = (int32_t)(xf + .5) ;//+ m_bx;
	// gotta be >= 0 it's a pixel
	if ( x2 < 0 ) {
		log("statsdb: bad x2 of %"INT32"",x2);
		return true;
	}
	// make this our y pos
	float y2 = y;
	// average values if tied
	if ( x2 == ss->m_lastx ) {
		// weighted average
		y2 = (y2*weight + 
		      ss->m_lasty * ss->m_lastWeight)/
			(weight+
			 ss->m_lastWeight);

		//logf(LOG_DEBUG,"statsdb: collision "
		//     "x=%"UINT32" x2=%"INT32" y=%.02f yt=%.02f",
		//     x,x2,y,y2);

		// update these
		ss->m_lasty      = y2;
		ss->m_lastWeight += weight ;
		// delete old point, we'll replace it
		m_sb1.popLong  ();
		m_sb1.popFloat ();
		m_sb1.popLong  ();
	}
	else {
		ss->m_lastx      = x2;
		ss->m_lasty      = y2;
		ss->m_lastWeight = weight;
	}

	// make room
	if ( ! m_sb1.reserve2x ( 64 ) ) return false;

	//logf(LOG_DEBUG,"statsdb: addPoint "
	//     "x=%"UINT32" x2=%"UINT32" y=%.02f y2=%.02f gh=%"UINT32"",x,x2,y,y2,graphHash);

	// store into array, safe buf
	m_sb1.pushLong  ( x2 );
	m_sb1.pushFloat ( y2 );
	m_sb1.pushLong  ( graphHash );
	return true;
}

//
// . add EventPoints to m_sb3/m_ht3
// . these basically represent binary events or parm state changes
// . i.e. "a merge operation"
// . i.e. "changing a parm value"
//
bool Statsdb::addEventPointsFromList ( ) {

	m_list.resetListPtr();
	// scan the list for our junk
	for ( ; ! m_list.isExhausted() ; m_list.skipCurrentRecord() ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get that
		StatKey *sk = (StatKey *)m_list.getCurrentRec();
		// and data
		StatData *sd = (StatData *)m_list.getCurrentData();
		// must be an "event" stat... i.e. a status change
		if ( ! sd->isEvent() ) continue;
		// make sure to stack lines so they do not touch
		// each other...
		if ( ! addEventPoint ( sk->m_time1       ,
				       sk->m_labelHash   , // parmHash
				       sd->getOldVal  () ,
				       sd->getNewVal  () ,
				       10                )) // thickness
			return false;
	}
	return true;
}

bool Statsdb::addEventPoint ( int32_t  t1        ,
			      int32_t  parmHash  ,
			      float oldVal    ,
			      float newVal    ,
			      int32_t  thickness ) {
	
	// convert t1 into pixel position
	float af = (float)DX2 * (float)(t1 - m_t1) / (float)(m_t2 - m_t1);
	// round it to nearest pixel
	int32_t  a = (int32_t)(af + .5) ;//+ m_bx;

	// convert t2 into pixel position
	//float bf = (float)DX2 * (float)(t2 - m_t1) / (float)(m_t2 - m_t1);
	// round it to nearest pixel
	//int32_t  b = (int32_t)(bf + .5) + m_bx;
	//if ( a > b ) { char *xx=NULL;*xx=0; }

	// 5 pixel width when rendering the square, 2 pixel boundary
	int32_t b = a + 7;

	// make sure we got it
	Parm *m = g_parms.getParmFromParmHash ( parmHash );
	if ( ! m ) { 
		log("statsdb: unrecognized parm hash = %"INT32"",parmHash);
		return true;
		//char *xx=NULL;*xx=0; }
	}

	// go down each line of points
	for ( int32_t i = 0 ; i < MAX_LINES2 ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// . is there room for us in this line?
		// . see what other lines/events are on this line
		int32_t slot = m_ht3.getSlot ( &i );
		// loop over all events on this line
		for ( ; slot >= 0 ; slot = m_ht3.getNextSlot ( slot , &i ) ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// get the offset
			int32_t offset = *(int32_t *)m_ht3.getValueFromSlot ( slot );
			// get buffer
			char *buf = m_sb3.getBufStart();
			// get its value
			EventPoint *p = (EventPoint *)(buf + offset);
			// check its boundaries, require 2 pixel spacing
			if ( a <  p->m_a && b >= p->m_a ) break;
			if ( b >  p->m_b && a <= p->m_b ) break;
			if ( a >= p->m_a && b <= p->m_b ) break;
		}
		// we collided with another event on this line, try next line
		if ( slot >= 0 ) continue;
		// make sure we got room
		if ( ! m_sb3.reserve2x ( sizeof(EventPoint) ) ) return false;
		// add it in
		EventPoint *pp = (EventPoint *)m_sb3.getBuf();
		// set it
		pp->m_a         = a;
		pp->m_b         = b;
		//pp->m_colorRGB  = colorRGB;
		pp->m_parmHash  = parmHash;
		pp->m_oldVal    = oldVal;
		pp->m_newVal    = newVal;
		pp->m_thickness = thickness;
		// store the offset incase m_sb3 reallocates
		int32_t length = m_sb3.length();
		// tell safebuf to skip over it now
		m_sb3.incrementLength ( sizeof(EventPoint) );
		// add line to hashtable
		if ( ! m_ht3.addKey ( (void *)&i , &length ) ) return false;
		// all done now
		return true;
	}
	// crap no room!
	log("stats: no room in graph for event");
	return true;
}

//////////
//
// NEW CODE HERE
//
//////////


// draw a HORIZONTAL line in html
void Statsdb::drawLine3 ( SafeBuf &sb ,
		 int32_t x1 , 
		 int32_t x2 ,
		 int32_t fy1 , 
		 int32_t color ,
		 int32_t width ) {

	// do not draw repeats in the case we have a ton of points to plot
	int32_t key32 ;
	key32 = hash32h ( x1  , 0 );
	key32 = hash32h ( x2  , key32);
	key32 = hash32h ( fy1 , key32);
	key32 = hash32h ( color , key32);
	key32 = hash32h ( width , key32);
	if ( m_dupTable.isInTable(&key32) ) return;
	m_dupTable.addKey(&key32);

	sb.safePrintf("<div style=\"position:absolute;"
				  "left:%"INT32";"
				  "bottom:%"INT32";"
				  "background-color:#%"XINT32";"
				  "z-index:-5;"
				  "min-height:%"INT32"px;"
				  "min-width:%"INT32"px;\""
				  " class=\"color-%"XINT32"\"></div>\n"
		      , x1 + m_bx
		      , (fy1 - width/2) + m_by
		      , color
		      , width
		      , x2 - x1
		      , color
		      );
}


