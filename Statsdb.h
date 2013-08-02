//
// Author: Matt Wells
//
// Rdb to store the stats for each host

#ifndef _STATSDB_H_
#define _STATSDB_H_

#include "zlib.h"
#include "Rdb.h"
#include "RdbList.h"
#include "Stats.h"
#include "Msg1.h"
#include "Msg4.h"
#include "SafeBuf.h"
#include "Hostdb.h"
#include "Threads.h"
#include "Process.h"
#include "HashTableX.h"
#include "SafeBuf.h"

#define ST_BUF_SIZE (32*1024)

class Statsdb {

 public:
	Statsdb();
	//~Statsdb();

	// reset m_rdb
	void reset() { m_rdb.reset(); };

	class Label *getLabel ( long graphHash );

	// initialize m_rdb
	bool init( );

	bool addColl ( char *coll, bool doVerify = false );

	void addDocsIndexed ( ) ;

	long getImgHeight() ;
	long getImgWidth() ;

	// so Process.cpp can turn it off when saving so we do not record
	// disk writes/reads
	bool m_disabled;

	// . returns false and sets g_errno on error, true otherwise
	// . we only add the stat to our local statsdb rdb, but because
	//   we might be dumping statsdb to disk or something it is possible
	//   we get an ETRYAGAIN error, so we try to accumulate stats in a
	//   local buffer in that case
	bool addStat ( long        niceness ,
		       char       *label    ,
		       long long   t1       ,
		       long long   t2       ,
		       float       value    , // y-value really, "numBytes"
		       long        parmHash = 0   ,
		       float       oldVal   = 0.0 ,
		       float       newVal   = 0.0 ,
		       long        userId32 = 0 );
	
	bool makeGIF ( long t1Arg , 
		       long t2Arg ,
		       long samples ,
		       // fill in the map key in html into "sb2"
		       SafeBuf *sb2 ,
		       void *state ,
		       void (* callback) (void *state) ,
		       long userId32 = 0 ) ;

	char *plotGraph ( char *pstart ,
			  char *pend ,
			  long graphHash ,
			  class GIFPlotter *plotter ,
			  long  zoff );

	void drawHR ( float z ,
		      float ymin , 
		      float ymax ,
		      class GIFPlotter *plotter ,
		      class Label *label ,
		      float zoff ,
		      long color ) ;

	bool gifLoop ( ) ;
	bool processList ( ) ;
	class StatState *getStatState ( long us ) ;
	// if graphVal is false we graph the count (num ops)
	bool addPointsFromStatsTable1 ( );
	bool addPointsFromStatsTable2 ( class Label *label ) ;

	bool addPointsFromList ( class Label *label );

	bool addPoint ( class StatKey   *sk , 
			class StatData  *sd ,
			class StatState *ss , 
			class Label *label ) ;

	bool addPoint ( long      x         ,
			float     y         ,
			//long      colorRGB  ,
			long      graphHash ,
			float     weight    ,
			class StatState *ss ) ;

	bool addEventPointsFromList ( );
	bool addEventPoint ( long  t1        ,
			     long  parmHash  ,
			     float oldVal    ,
			     float newVal    ,
			     long  thickness ) ;


	Rdb *getRdb() { return &m_rdb; }

	Rdb 	  m_rdb;
	RdbList   m_list;
	Msg1	  m_msg1;

	SafeBuf m_sb0;
	SafeBuf m_sb1;

	SafeBuf m_sb3;

	SafeBuf *m_sb2;

	HashTableX m_ht0;
	HashTableX m_ht3;

	HashTableX m_labelTable;

	long m_niceness;

	// pixel boundaries
	long m_bx;
	long m_by;

	// # of samples in moving avg
	long m_samples;

	void *m_state;
	void (*m_callback )(void *state);

	// some flag
	bool m_done;

	key_t m_startKey;
	key_t m_endKey;

	FILE *m_fd;

	Msg5 m_msg5;

	// time window to graph
	long m_t1;
	long m_t2;

	bool m_init;
};

extern class Statsdb g_statsdb;

// . we now take a snapshot every one second and accumulate all stats
//   ending a Stat::m_time1 for that second into this Stat
// . so basically Statsdb::addStat() accumulates stats for each second
//   based on the label hash
class StatKey {
 public:
	// these two vars make up the key_t!
	unsigned long m_zero;
	unsigned long m_labelHash;
	time_t        m_time1;
};

class StatData {
 public:
	float      m_totalOps;
	float      m_totalQuantity; // aka "m_oldVal"
	float      m_totalTime; // in SECONDS!

	float      m_newVal;

	// set the m_key members based on the data members
	float     getOldVal () { return m_totalQuantity; }; // aliased
	float     getNewVal () { return m_newVal ; };
	bool      isStatusChange() { return (m_totalOps==0); };
	bool      isEvent       () { return (m_totalOps==0); };
	//void      setKey ( long long t1 , unsigned long labelHash ) {
	//	m_key.n1 = t1; m_key.n0 = labelHash; };
	//long      getLabelHash () { return (long)m_labelHash; };
	//long      getParmHash  () { return (long)m_labelHash; };
	//long      getTime1     () { return m_time1; };
};

#endif
