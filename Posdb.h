// Matt Wells, Copyright May 2012

// . format of an 18-byte posdb key
//   tttttttt tttttttt tttttttt tttttttt  t = termId (48bits)
//   tttttttt tttttttt dddddddd dddddddd  d = docId (38 bits)
//   dddddddd dddddddd dddddd0r rrrggggg  r = siterank, g = langid
//   wwwwwwww wwwwwwww wwGGGGss ssvvvvFF  w = word postion , s = wordspamrank
//   pppppb1N MMMMLZZD                    v = diversityrank, p = densityrank
//                                        M = multiplier, b = in outlink text
//                                        L = langIdShiftBit (upper bit)
//   G: 0 = body 
//      1 = intitletag 
//      2 = inheading 
//      3 = inlist 
//      4 = inmetatag
//      5 = inlinktext
//      6 = tag
//      7 = inneighborhood
//      8 = internalinlinktext
//      9 = inurl
//
//   F: 0 = original term
//      1 = conjugate/sing/plural
//      2 = synonym
//      3 = hyponym

//   NOTE: N bit is 1 if the shard of the record is determined by the
//   termid (t bits) and NOT the docid (d bits). N stands for "nosplit"
//   and you can find that logic in XmlDoc.cpp and Msg4.cpp. We store 
//   the hash of the content like this so we can see if it is a dup.

//   NOTE: M bits hold scaling factor (logarithmic) for link text voting
//   so we do not need to repeat the same link text over and over again.
//   Use M bits to hold # of inlinks the page has for other terms.

//   NOTE: for inlinktext terms the spam rank is the siterank of the
//   inlinker!

//   NOTE: densityrank for title is based on # of title words only. same goes
//   for incoming inlink text.

//   NOTE: now we can b-step into the termlist looking for a docid match
//   and not worry about misalignment from the double compression scheme
//   because if the 6th byte's low bit is clear that means its a docid
//   12-byte key, otherwise its the word position 6-byte key since the delbit
//   can't be clear for those!

//   THEN we can play with a tuner for how these various things affect
//   the search results ranking.


#ifndef _POSDB_H_
#define _POSDB_H_

#include "Rdb.h"
#include "Conf.h"
//#include "Indexdb.h"
#include "Titledb.h" // DOCID_MASK
#include "HashTableX.h"
#include "Sections.h"

#define MAXSITERANK      0x0f // 4 bits
#define MAXLANGID        0x3f // 6 bits (5 bits go in 'g' the other in 'L')
#define MAXWORDPOS       0x0003ffff // 18 bits
#define MAXDENSITYRANK   0x1f // 5 bits
#define MAXWORDSPAMRANK  0x0f // 4 bits
#define MAXDIVERSITYRANK 0x0f // 4 bits
#define MAXHASHGROUP     0x0f // 4 bits
#define MAXMULTIPLIER    0x0f // 4 bits
#define MAXISSYNONYM     0x03 // 2 bits

// values for G bits in the posdb key
#define HASHGROUP_BODY                 0 // body implied
#define HASHGROUP_TITLE                1 
#define HASHGROUP_HEADING              2 // body implied
#define HASHGROUP_INLIST               3 // body implied
#define HASHGROUP_INMETATAG            4
#define HASHGROUP_INLINKTEXT           5
#define HASHGROUP_INTAG                6
#define HASHGROUP_NEIGHBORHOOD         7
#define HASHGROUP_INTERNALINLINKTEXT   8
#define HASHGROUP_INURL                9
#define HASHGROUP_INMENU               10 // body implied
#define HASHGROUP_END                  11

float getDiversityWeight ( unsigned char diversityRank );
float getDensityWeight   ( unsigned char densityRank );
float getWordSpamWeight  ( unsigned char wordSpamRank );
float getLinkerWeight    ( unsigned char wordSpamRank );
char *getHashGroupString ( unsigned char hg );
float getHashGroupWeight ( unsigned char hg );
float getTermFreqWeight  ( int64_t termFreq , int64_t numDocsInColl );

#define SYNONYM_WEIGHT 0.90
#define WIKI_WEIGHT    0.10 // was 0.20
#define SITERANKDIVISOR 3.0
#define SITERANKMULTIPLIER 0.33333333
//#define SAMELANGMULT    20.0 // FOREIGNLANGDIVISOR  2.0

#define POSDBKEY key144_t

#define BF_HALFSTOPWIKIBIGRAM 0x01  // "to be" in "to be or not to be"
#define BF_PIPED              0x02  // before a query pipe operator
#define BF_SYNONYM            0x04
#define BF_NEGATIVE           0x08  // query word has a negative sign before it
#define BF_BIGRAM             0x10  // query word has a negative sign before it
#define BF_NUMBER             0x20  // is it like gbsortby:price? numeric?
#define BF_FACET              0x40  // gbfacet:price

void printTermList ( int32_t i, char *list, int32_t listSize ) ;

// if query is 'the tigers' we weight bigram "the tigers" x 1.20 because
// its in wikipedia.
// up this to 1.40 for 'the time machine' query
#define WIKI_BIGRAM_WEIGHT 1.40

class Posdb {

 public:

	// resets rdb
	void reset();

	// sets up our m_rdb from g_conf (global conf class)
	bool init ( );

	// init the rebuild/secondary rdb, used by PageRepair.cpp
	bool init2 ( int32_t treeMem );

	bool verify ( char *coll );

	bool addColl ( char *coll, bool doVerify = true );

	// . xmldoc.cpp should call this
	// . store all posdb keys from revdbList into one hashtable
	//   and only add to new list if not in there
	//bool makeList ( class RdbList *revdbList ,
	//		int64_t docId ,
	//		class Words *words );
			


	// . make a 16-byte key from all these components
	// . since it is 16 bytes, the big bit will be set
	void makeKey ( void              *kp             ,
		       int64_t          termId         ,
		       uint64_t docId          , 
		       int32_t               wordPos        ,
		       char               densityRank    ,
		       char               diversityRank  ,
		       char               wordSpamRank   ,
		       char               siteRank       ,
		       char               hashGroup      ,
		       char               langId         ,
		       // multiplier: we convert into 7 bits in this function
		       int32_t               multiplier     ,
		       bool               isSynonym      ,
		       bool               isDelKey       ,
		       bool               shardByTermId  );

	// make just the 6 byte key
	void makeKey48 ( char              *kp             ,
			 int32_t               wordPos        ,
			 char               densityRank    ,
			 char               diversityRank  ,
			 char               wordSpamRank   ,
			 char               hashGroup      ,
			 char               langId         ,
			 bool               isSynonym      ,
			 bool               isDelKey       );


	int printList ( RdbList &list ) ;

	// we map the 32bit score to like 7 bits here
	void setMultiplierBits ( void *vkp , unsigned char mbits ) {
		key144_t *kp = (key144_t *)vkp;
		if ( mbits > MAXMULTIPLIER ) { char *xx=NULL;*xx=0; }
		kp->n0 &= 0xfc0f;
		// map score to bits
		kp->n0 |= ((uint16_t)mbits) << 4;
	}
	
	void setDocIdBits ( void *vkp , uint64_t docId ) {
		key144_t *kp = (key144_t *)vkp;
		kp->n1 &= 0x000003ffffffffffLL;
		kp->n1 |= (docId<<(32+10));
		kp->n2 &= 0xffffffffffff0000LL;
		kp->n2 |= docId>>22;
	}
	
	void setSiteRankBits ( void *vkp , char siteRank ) {
		key144_t *kp = (key144_t *)vkp;
		if ( siteRank > MAXSITERANK ) { char *xx=NULL;*xx=0; }
		kp->n1 &= 0xfffffe1fffffffffLL;
		kp->n1 |= ((uint64_t)siteRank)<<(32+5);
	}
	
	void setLangIdBits ( void *vkp , char langId ) {
		key144_t *kp = (key144_t *)vkp;
		if ( langId > MAXLANGID ) { char *xx=NULL;*xx=0; }
		kp->n1 &= 0xffffffe0ffffffffLL;
		// put the lower 5 bits here
		kp->n1 |= ((uint64_t)(langId&0x1f))<<(32);
		// and the upper 6th bit here. n0 is a int16_t.
		// 0011 1111
		if ( langId & 0x20 ) kp->n0 |= 0x08;
	}

	// set the word position bits et al to this float
	void setFloat ( void *vkp , float f ) {
		*(float *)(((char *)vkp) + 2) = f; };

	void setInt ( void *vkp , int32_t x ) {
		*(int32_t *)(((char *)vkp) + 2) = x; };

	// and read the float as well
	float getFloat ( void *vkp ) {
		return *(float *)(((char *)vkp) + 2); };

	int32_t getInt ( void *vkp ) {
		return *(int32_t *)(((char *)vkp) + 2); };

	void setAlignmentBit ( void *vkp , char val ) {
		char *p = (char *)vkp;
		if ( val ) p[1] = p[1] | 0x02;
		else       p[1] = p[1] & 0xfd;
	};

	bool isAlignmentBitClear ( void *vkp ) {
		return ( ( ((char *)vkp)[1] & 0x02 ) == 0x00 );
	};

	void makeStartKey ( void *kp, int64_t termId , 
			    int64_t docId=0LL){
		return makeKey ( kp,
				 termId , 
				 docId,
				 0, // wordpos
				 0, // density
				 0, // diversity
				 0, // wordspam
				 0, // siterank
				 0, // hashgroup
				 0, // langid
				 0, // multiplier
				 0, // issynonym/etc.
				 true ,  // isdelkey
				 false ); // shardbytermid?
	};

	void makeEndKey  ( void *kp,int64_t termId, 
			   int64_t docId = MAX_DOCID ) {
		return makeKey ( kp,
				 termId , 
				 docId,
				 MAXWORDPOS,
				 MAXDENSITYRANK,
				 MAXDIVERSITYRANK,
				 MAXWORDSPAMRANK,
				 MAXSITERANK,
				 MAXHASHGROUP,
				 MAXLANGID,
				 MAXMULTIPLIER,
				 MAXISSYNONYM, // issynonym/etc.
				 false, // isdelkey
				 true);// shard by termid?
	};

	// we got two compression bits!
	unsigned char getKeySize ( void *key ) {
		if ( (((char *)key)[0])&0x04 ) return 6;
		if ( (((char *)key)[0])&0x02 ) return 12;
		return 18;
	};

	// PosdbTable uses this to skip from one docid to the next docid
	// in a posdblist
	char *getNextDocIdSublist ( char *p ,  char *listEnd ) {
		// key must be 12
		//if ( getKeySize(p) != 12 ) { char *xx=NULL;*xx=0; }
		// skip that first key
		p += 12;
		// skip the 6 byte keys
		for ( ; p < listEnd && getKeySize(p) == 6 ; p += 6 );
		// done
		return p;
	}
		

	int64_t getTermId ( void *key ) {
		return ((key144_t *)key)->n2 >> 16;
	};

	int64_t getDocId ( void *key ) {
		uint64_t d = 0LL;
		d = ((unsigned char *)key)[11];
		d <<= 32;
		d |= *(uint32_t *)(((unsigned char *)key)+7);
		d >>= 2;
		return d;
		//int64_t d = ((key144_t *)key)->n2 & 0xffff;
		//d <<= 22;
		//d |= ((key144_t *)key)->n1 >> (32+8+2);
		//return d;
	};

	unsigned char getSiteRank ( void *key ) {
		return (((key144_t *)key)->n1 >> 37) & MAXSITERANK;
	};

	unsigned char getLangId ( void *key ) {
		if ( ((char *)key)[0] & 0x08 )
			return ((((key144_t *)key)->n1 >> 32) & 0x1f) | 0x20;
		else
			return ((((key144_t *)key)->n1 >> 32) & 0x1f) ;
	};

	unsigned char getHashGroup ( void *key ) {
		//return (((key144_t *)key)->n1 >> 10) & MAXHASHGROUP;
		return ((((unsigned char *)key)[3]) >>2) & MAXHASHGROUP;
	};

	int32_t getWordPos ( void *key ) {
		//return (((key144_t *)key)->n1 >> 14) & MAXWORDPOS;
		return (*((uint32_t *)((unsigned char *)key+2))) >> (8+6);
	};

	inline void setWordPos ( char *key , uint32_t wpos ) {
		// truncate
		wpos &= MAXWORDPOS;
		if ( wpos & 0x01 ) key[3] |= 0x40;
		else               key[3] &= ~((unsigned char)0x40);
		if ( wpos & 0x02 ) key[3] |= 0x80;
		else               key[3] &= ~((unsigned char)0x80);
		wpos >>= 2;
		key[4] = ((char *)&wpos)[0];
		key[5] = ((char *)&wpos)[1];
	};

	unsigned char getWordSpamRank ( void *key ) {
		//return (((key144_t *)key)->n1 >> 6) & MAXWORDSPAMRANK;
		return ((((uint16_t *)key)[1]) >>6) & MAXWORDSPAMRANK;
	};

	unsigned char getDiversityRank ( void *key ) {
		//return (((key144_t *)key)->n1 >> 2) & MAXDIVERSITYRANK;
		return ((((unsigned char *)key)[2]) >>2) & MAXDIVERSITYRANK;
	};

	unsigned char getIsSynonym ( void *key ) {
		return (((key144_t *)key)->n1 ) & 0x03;
	};

	unsigned char getIsHalfStopWikiBigram ( void *key ) {
		return ((char *)key)[2] & 0x01;
	};

	unsigned char getDensityRank ( void *key ) {
		return ((*(uint16_t *)key) >> 11) & MAXDENSITYRANK;
	};

	inline void setDensityRank ( char *key , unsigned char dr ) {
		// shift up
		dr <<= 3;
		// clear out
		key[1] &= 0x07;
		// or in
		key[1] |= dr;
	};

	char isShardedByTermId ( void *key ){return ((char *)key)[1] & 0x01; };

	void setShardedByTermIdBit ( void *key ) { 
		char *k = (char *)key;
		k[1] |= 0x01;
	};

	unsigned char getMultiplier ( void *key ) {
		return ((*(uint16_t *)key) >> 4) & MAXMULTIPLIER; };

	// . HACK: for sectionhash:xxxxx posdb keys
	// . we use the w,G,s,v and F bits
	uint32_t getFacetVal32 ( void *key ) {
		return *(uint32_t *)(((char *)key)+2); };
	void setFacetVal32 ( void *key , int32_t facetVal32 ) {
		*(uint32_t *)(((char *)key)+2) = facetVal32; };

	int64_t getTermFreq ( collnum_t collnum, int64_t termId ) ;

	//RdbCache *getCache ( ) { return &m_rdb.m_cache; };
	Rdb      *getRdb   ( ) { return &m_rdb; };

	Rdb m_rdb;

	//DiskPageCache *getDiskPageCache ( ) { return &m_pc; };

	//DiskPageCache m_pc;
};

class FacetEntry {
 public:
	// # of search results that have this value:
	int32_t m_count;
	// # of docs that have this value:
	int32_t m_outsideSearchResultsCount;
	int64_t m_docId;

	// cast as double/floats for floats:
	int64_t m_sum;
	int32_t m_max;
	int32_t m_min;
};



#define MAX_SUBLISTS 50

// . each QueryTerm has this attached additional info now:
// . these should be 1-1 with query terms, Query::m_qterms[]
class QueryTermInfo {
public:
	class QueryTerm *m_qt;
	// the required lists for this query term, synonym lists, etc.
	RdbList  *m_subLists        [MAX_SUBLISTS];
	// flags to indicate if bigram list should be scored higher
	char      m_bigramFlags     [MAX_SUBLISTS];
	// shrinkSubLists() set this:
	int32_t      m_newSubListSize  [MAX_SUBLISTS];
	char     *m_newSubListStart [MAX_SUBLISTS];
	char     *m_newSubListEnd   [MAX_SUBLISTS];
	char     *m_cursor          [MAX_SUBLISTS];
	char     *m_savedCursor     [MAX_SUBLISTS];
	// the corresponding QueryTerm for this sublist
	//class QueryTerm *m_qtermList [MAX_SUBLISTS];
	int32_t      m_numNewSubLists;
	// how many are valid?
	int32_t      m_numSubLists;
	// size of all m_subLists in bytes
	int64_t m_totalSubListsSize;
	// the term freq weight for this term
	float     m_termFreqWeight;
	// what query term # do we correspond to in Query.h
	int32_t      m_qtermNum;
	// the word position of this query term in the Words.h class
	int32_t      m_qpos;
	// the wikipedia phrase id if we start one
	int32_t      m_wikiPhraseId;
	// phrase id term or bigram is in
	int32_t      m_quotedStartId;
};


/*
#include "RdbList.h"

class PosdbList : public RdbList {

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

	// clear the low bits on the keys so terms are DELETED
	void clearDelBits ( );

	void print();


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
*/

#include "Query.h"         // MAX_QUERY_TERMS, qvec_t

// max # search results that can be viewed without using TopTree
//#define MAX_RESULTS 1000

class PosdbTable {

 public:

	// . returns false on error and sets errno
	// . "termFreqs" are 1-1 with q->m_qterms[]
	// . sets m_q to point to q
	void init (Query         *q               ,
		   char           debug           ,
		   void          *logstate        ,
		   class TopTree *topTree         ,
		   //char          *coll            ,
		   collnum_t collnum ,
		   //IndexList     *lists           ,
		   //int32_t           numLists        ,
		   class Msg2 *msg2, 
		   class          Msg39Request *r );

	// pre-allocate m_whiteListTable
	bool allocWhiteListTable ( ) ;

	// pre-allocate memory since intersection runs in a thread
	bool allocTopTree ( );

	// . returns false on error and sets errno
	// . we assume there are "m_numTerms" lists passed in (see set() above)
	//void intersectLists_r ( );

	//void intersectLists9_r ( );

	void  getTermPairScoreForNonBody   ( int32_t i, int32_t j,
					     char *wpi, char *wpj, 
					     char *endi, char *endj,
					     int32_t qdist ,
					     float *retMax );
	float getSingleTermScore ( int32_t i, char *wpi , char *endi,
				   class DocIdScore *pdcs,
				   char **bestPos );

	void evalSlidingWindow ( char **ptrs , 
				 int32_t   nr , 
				 char **bestPos ,
				 float *scoreMatrix  ,
				 int32_t   advancedTermNum );
	float getTermPairScoreForWindow ( int32_t i, int32_t j,
					  char *wpi,
					  char *wpj,
					  int32_t fixedDistance
					  );

	float getTermPairScoreForAny   ( int32_t i, int32_t j,
					 char *wpi, char *wpj, 
					 char *endi, char *endj,
					 class DocIdScore *pdcs );

	bool makeDocIdVoteBufForBoolQuery_r ( ) ;

	// some generic stuff
	PosdbTable();
	~PosdbTable();
	void reset();

	// Msg39 needs to call these
	void freeMem ( ) ;

	// has init already been called?
	bool isInitialized ( ) { return m_initialized; };

	uint64_t m_docId;

	uint64_t m_docIdHack;

	bool m_hasFacetTerm;

	bool m_hasMaxSerpScore;

	// hack for seo.cpp:
	float m_finalScore;
	float m_preFinalScore;

	float m_siteRankMultiplier;

	// how long to add the last batch of lists
	int64_t       m_addListsTime;
	int64_t       m_t1 ;
	int64_t       m_t2 ;

	int64_t       m_estimatedTotalHits;

	int32_t            m_errno;

	int32_t            m_numSlots;

	int32_t            m_maxScores;

	//char           *m_coll;
	collnum_t       m_collnum;

	int32_t *m_qpos;
	int32_t *m_wikiPhraseIds;
	int32_t *m_quotedStartIds;
	//class DocIdScore *m_ds;
	int32_t  m_qdist;
	float *m_freqWeights;
	//int64_t *m_freqs;
	char  *m_bflags;
	int32_t  *m_qtermNums;
	float m_bestWindowScore;
	//char **m_finalWinners1;
	//char **m_finalWinners2;
	//float *m_finalScores;
	char **m_windowTermPtrs;

	// how many docs in the collection?
	int64_t m_docsInColl;

	//SectionStats m_sectionStats;
	//SafeBuf m_facetHashList;
	//HashTableX m_dt;

	class Msg2 *m_msg2;

	// if getting more than MAX_RESULTS results, use this top tree to hold
	// them rather than the m_top*[] arrays above
	class TopTree *m_topTree;

	//HashTableX m_docIdTable;
	
	SafeBuf m_scoreInfoBuf;
	SafeBuf m_pairScoreBuf;
	SafeBuf m_singleScoreBuf;

	SafeBuf m_stackBuf;

	//SafeBuf m_mergeBuf;

	// a reference to the query
	Query          *m_q;
	int32_t m_nqt;

	// these are NOT in imap space, but in query term space, 1-1 with 
	// Query::m_qterms[]
	//IndexList      *m_lists;
	//int32_t            m_numLists;

	// has init() been called?
	bool            m_initialized;

	// are we in debug mode?
	char            m_debug;

	// for debug msgs
	void *m_logstate;

	//int64_t       m_numDocsInColl;

	class Msg39Request *m_r;

	// for gbsortby:item.price ...
	int32_t m_sortByTermNum;
	int32_t m_sortByTermNumInt;

	// fix core with these two
	int32_t m_sortByTermInfoNum;
	int32_t m_sortByTermInfoNumInt;

	// for gbmin:price:1.99
	int32_t m_minScoreTermNum;
	int32_t m_maxScoreTermNum;

	// for gbmin:price:1.99
	float m_minScoreVal;
	float m_maxScoreVal;

	// for gbmin:count:99
	int32_t m_minScoreTermNumInt;
	int32_t m_maxScoreTermNumInt;

	// for gbmin:count:99
	int32_t m_minScoreValInt;
	int32_t m_maxScoreValInt;


	// the new intersection/scoring algo
	void intersectLists10_r ( );	

	HashTableX m_whiteListTable;
	bool m_useWhiteTable;
	bool m_addedSites;

	// sets stuff used by intersect10_r()
	bool setQueryTermInfo ( );

	void shrinkSubLists ( class QueryTermInfo *qti );

	int64_t countUniqueDocids( QueryTermInfo *qti ) ;

	// for intersecting docids
	void addDocIdVotes ( class QueryTermInfo *qti , int32_t listGroupNum );

	// for negative query terms...
	void rmDocIdVotes ( class QueryTermInfo *qti );

	// upper score bound
	float getMaxPossibleScore ( class QueryTermInfo *qti ,
				    int32_t bestDist ,
				    int32_t qdist ,
				    class QueryTermInfo *qtm ) ;

	// stuff set in setQueryTermInf() function:
	SafeBuf              m_qiBuf;
	int32_t                 m_numQueryTermInfos;
	// the size of the smallest set of sublists. each sublists is
	// the main term or a synonym, etc. of the main term.
	int32_t                 m_minListSize;
	// which query term info has the smallest set of sublists
	int32_t                 m_minListi;
	// intersect docids from each QueryTermInfo into here
	SafeBuf              m_docIdVoteBuf;

	int32_t m_filtered;

	// boolean truth table for boolean queries
	HashTableX m_bt;
	HashTableX m_ct;
	// size of the data slot in m_bt
	int32_t m_vecSize;

	// are all positive query terms in same wikipedia phrase like
	// 'time enough for love'?
	bool m_allInSameWikiPhrase;

	int32_t m_realMaxTop;
};

#define MAXDST 10

// distance used when measuring word from title/linktext/etc to word in body
#define FIXED_DISTANCE 400

class PairScore {
 public:
	float m_finalScore;
	char  m_isSynonym1;
	char  m_isSynonym2;
	char  m_isHalfStopWikiBigram1;
	char  m_isHalfStopWikiBigram2;
	char  m_diversityRank1;
	char  m_diversityRank2;
	char  m_densityRank1;
	char  m_densityRank2;
	char  m_wordSpamRank1;
	char  m_wordSpamRank2;
	char  m_hashGroup1;
	char  m_hashGroup2;
	char  m_inSameWikiPhrase;
	char  m_fixedDistance;
	int32_t  m_wordPos1;
	int32_t  m_wordPos2;
	int64_t m_termFreq1;
	int64_t m_termFreq2;
	float     m_tfWeight1;
	float     m_tfWeight2;
	int32_t m_qtermNum1;
	int32_t m_qtermNum2;
	char m_bflags1;
	char m_bflags2;
	int32_t m_qdist;
};

class SingleScore {
 public:
	float m_finalScore;
	char  m_isSynonym;
	char  m_isHalfStopWikiBigram;
	char  m_diversityRank;
	char  m_densityRank;
	char  m_wordSpamRank;
	char  m_hashGroup;
	int32_t  m_wordPos;
	int64_t m_termFreq; // float m_termFreqWeight;
	float m_tfWeight;
	int32_t m_qtermNum;
	char m_bflags;
};

// we add up the pair scores of this many of the top-scoring pairs
// for inlink text only, so it is accumulative. but now we also
// have a parm "m_realMaxTop" which is <= MAX_TOP and can be used to
// tune this down.
#define MAX_TOP 10

// transparent query scoring info per docid
class DocIdScore {
 public:
	DocIdScore ( ) { reset(); }

	void reset ( ) {
		m_numPairs = m_numSingles = 0;
		m_pairsOffset = m_singlesOffset = -1;
		m_pairScores = NULL;
		m_singleScores = NULL;
	};

	// we use QueryChange::getDebugDocIdScore() to "deserialize" per se
	bool serialize   ( class SafeBuf *sb );

	int64_t   m_docId;
	// made this a double because of intScores which can't be captured
	// fully with a float. intScores are used to sort by spidered time
	// for example. see Posdb.cpp "intScore".
	double      m_finalScore;
	char        m_siteRank;
	int32_t        m_docLang; // langId
	int32_t        m_numRequiredTerms;

	int32_t m_numPairs;
	int32_t m_numSingles;

	// . m_pairScores is just all the term pairs serialized
	// . they contain their query term #1 of each term in the pair and
	//   they have the match number for each pair, since now each
	//   pair of query terms can have up to MAX_TOP associated pairs
	//   whose scores we add together to get the final score for that pair
	// . record offset into PosdbTable::m_pairScoreBuf
	// . Msg39Reply::ptr_pairScoreBuf will be this
	int32_t m_pairsOffset;
	// . record offset into PosdbTable.m_singleScoreBuf
	// . Msg39Reply::ptr_singleScoreBuf will be this
	int32_t m_singlesOffset;
	//PairScore   m_pairScores  [MAXDST][MAXDST][MAX_TOP];
	//SingleScore m_singleScores[MAXDST]        [MAX_TOP];

	// Msg3a.cpp::mergeLists() should set these ptrs after it
	// copies over a top DocIdScore for storing the final results array
	class PairScore   *m_pairScores;
	class SingleScore *m_singleScores;
};


extern Posdb g_posdb;
extern Posdb g_posdb2;
extern RdbCache g_termFreqCache;

// . b-step into list looking for docid "docId"
// . assume p is start of list, excluding 6 byte of termid
inline char *getWordPosList ( int64_t docId , char *list , int32_t listSize ) {
	// make step divisible by 6 initially
	int32_t step = (listSize / 12) * 6;
	// int16_tcut
	char *listEnd = list + listSize;
	// divide in half
	char *p = list + step;
	// for detecting not founds
	char count = 0;
 loop:
	// save it
	char *origp = p;
	// scan up to docid. we use this special bit to distinguish between
	// 6-byte and 12-byte posdb keys
	for ( ; p > list && (p[1] & 0x02) ; p -= 6 );
	// ok, we hit a 12 byte key i guess, so backup 6 more
	p -= 6;
	// ok, we got a 12-byte key then i guess
	int64_t d = g_posdb.getDocId ( p );
	// we got a match, but it might be a NEGATIVE key so
	// we have to try to find the positive keys in that case
	if ( d == docId ) {
		// if its positive, no need to do anything else
		if ( (p[0] & 0x01) == 0x01 ) return p;
		// ok, it's negative, try to see if the positive is
		// in here, if not then return NULL.
		// save current pos
		char *current = p;
		// back up to 6 byte key before this 12 byte key
		p -= 6;
		// now go backwards to previous 12 byte key
		for ( ; p > list && (p[1] & 0x02) ; p -= 6 );
		// ok, we hit a 12 byte key i guess, so backup 6 more
		p -= 6;
		// is it there?
		if ( p >= list && g_posdb.getDocId(p) == docId ) {
			// sanity. return NULL if its negative! wtf????
			if ( (p[0] & 0x01) == 0x00 ) return NULL;
			// got it
			return p;
		}
		// ok, no positive before us, try after us
		p = current;
		// advance over current 12 byte key
		p += 12;
		// now go forwards to next 12 byte key
		for ( ; p < listEnd && (p[1] & 0x02) ; p += 6 );
		// is it there?
		if ( p + 12 < listEnd && g_posdb.getDocId(p) == docId ) {
			// sanity. return NULL if its negative! wtf????
			if ( (p[0] & 0x01) == 0x00 ) return NULL;
			// got it
			return p;
		}
		// . crap, i guess just had a single negative docid then
		// . return that and the caller will see its negative
		return current;
	}		
	// reduce step
	//step /= 2;
	step >>= 1;
	// . make divisible by 6!
	// . TODO: speed this up!!!
	step = step - (step % 6);
	// sanity
	if ( step % 6 ) { char *xx=NULL;*xx=0; }
	// ensure never 0
	if ( step <= 0 ) {
		step = 6;
		// return NULL if not found
		if ( count++ >= 2 ) return NULL;
	}
	// go up or down then
	if ( d < docId ) { 
		p = origp + step;
		if ( p > listEnd ) p = listEnd - 6;
	}
	else {
		p = origp - step;
		if ( p < list ) p = list;
	}
	// and repeat
	goto loop;
}

#endif
