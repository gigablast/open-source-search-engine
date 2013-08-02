#ifndef _SEO_H_
#define _SEO_H_

#include "gb-include.h"
#include "Mem.h" // gbstrlen

// . scalar to convert gb daily impression estimates to google
// . multiply by 31 to go monthly, 60 for google
#define GB_TRAFFIC_MODIFIER (60*31)


bool setQueryInfo ( char *qstr , class QueryInfo *qi ) ;

// so main.cpp can register it
void runSEOQueryLoop ( int fd , void *state ) ;

void handleRequest8e ( class UdpSlot *slot , long netnice ) ;
void handleRequest4f ( class UdpSlot *slot , long netnice ) ;
void handleRequest95 ( class UdpSlot *slot , long netnice ) ;
bool loadQueryLog();

long long getSynBaseHash64 ( char *qstr , uint8_t langId ) ;

extern char *g_secret_tran_key;
extern char *g_secret_api_key;

/*
class Msg3fRequest {
 public:

	// document language:
	uint8_t m_langId3f; 
	long    m_niceness;

	// first is coll
	char *ptr_coll;
	// termlistbuf for doing msg39 queries
	char *ptr_termListBuf;
	
	long  size_coll;
	long  size_termListBuf;
};
*/

// when we insert an insertable term into a document, how does it affect
// the document's ranking for a particular query?
class QueryChange {
public:
	// . what term are we inserting here
	// . offset is relative to the Msg95Request::ptr_insertableTerms
	//   which are strings separated by \0's
	// . this is just an offset into the XmlDoc::m_itStrBuf safebuf
	long  m_termStrOffset;
	// we scan all matching queries for this term/position combo, so
	// this is the offset of the QueryLogEntry in our local g_qbuf. 
	long  m_queryOffset3;
	// this is the one that is relative the Msg95Reply::ptr_queryLogBuf.
	// the other one is relative to the remote host's g_qbuf QueryLogEntry
	// buf.
	long  m_replyQueryOffset;
	// for debugging, offset into m_debugScoreInfoBuf
	long  m_debugScoreInfoOffset;
	// for debugging, offset into m_origScoreInfoBuf
	long  m_origScoreInfoOffset;
	// total traffic the query gets per day
	//long  m_dailyTraffic;

	
	// for matching to our InsertableTerms when done
	long long m_termHash64;
	long      m_queryHash32;

	// and the term's position range this applies to
	long  m_insertPos;//termPosition;
	// the original score
	float m_oldScore;
	// the new score with term insertion
	float m_newScore;
	// the old rank
	char  m_oldRank;
	// and new rank with term insertion
	char  m_newRank;
	// this hostid, handling the msg95
	//short m_hostId;

	// the linked list we are in
	class QueryChange *m_next;

	// this makes sure DocIdScore::m_pairScores ptr is correct
	class DocIdScore *getDebugDocIdScore ( SafeBuf *debugScoreInfoBuf ) ;
	// this makes sure DocIdScore::m_singleScores ptr is correct
	class DocIdScore *getOrigDocIdScore ( SafeBuf *origScoreInfoBuf ) ;
};

// . of all the alnum
// . we have one of these for every alnum word in the doc that we
//   index into posdb. basically we make this from the list of posdb
//   keys. we pass it in through Msg95Request to handleRequest95.
class WordPosInfo {
public:
	long  m_wordPos;
	long  m_sentNum;
	char  m_hashGroup;
	char  m_densityRank;
	char  m_wordSpamRank; // doubles as siterank for link text hashgroup
	char  m_diversityRank;
	char *m_wordPtr;
	long  m_wordLen;
	// traffic gain for inserting the selected insertable term into
	// this word position. this is set in seo.cpp
	long  m_trafficGain;
	// holds the symbol info, the glyph to display
	char m_color;
};

// put these in the safebuf now
class TermInfo {
public:
	unsigned long long m_termId64;
	//long long m_termFreq64;
};


/*	
class WordFreqInfo {
 public:
	// 32 bit termid
	long long m_wordId64;
	// it's term freq
	long long m_wordFreq64;
};
*/

// used to get matching queries for the main url or a related docid
class Msg95Request {

 public:
	long long m_docId;

	uint8_t m_docLangId;

	// debug mode?
	char m_seoDebug;

	// basically all the posdb keys, all are full 18-byte keys. no
	// compression for simplicity. used to pass posdb termlists into
	// msg39 just for this m_docid
	char *ptr_posdbTermList;
	// Top Word Ids buffer. basically all the word ids contained
	// in the doc (m_docId) or in the inlink text for the doc
	char *ptr_termInfoBuf;//twid32Buf;
	// array of WordFreqInfos from host #0 only for consistency
	// so msg95 handler can use them for calling msg39
	//char *ptr_wordFreqInfoBuf; 
	// instances of the WordInfo class that give us sentence #, hashgroup,
	// word position of every word in the doc. sorted by m_wordPos.
	char *ptr_wordPosInfoBuf;
	char *ptr_coll;
	// \0 separated list of the insertable phrases to try to insert
	// into every possible position in the document and see how much
	// our traffic will increase/decrease by doing so. each
	// insertable term can be a single word or sequence of words.
	char *ptr_insertableTerms;
	long  size_posdbTermList;
	long  size_termInfoBuf; // twid32Buf;
	//long  size_wordFreqInfoBuf;
	long  size_wordPosInfoBuf;
	long  size_coll;
	long  size_insertableTerms;
	char  m_buf[0];
};

class Msg95Reply {

 public:
	char *ptr_queryChangeBuf ;
	char *ptr_debugScoreInfoBuf;
	char *ptr_origScoreInfoBuf;
	char *ptr_queryLogBuf    ;

	long  size_queryChangeBuf;
	long  size_debugScoreInfoBuf;
	long  size_origScoreInfoBuf;
	long  size_queryLogBuf   ;

	char  m_buf[0];
};

// get the top 300 results of each query we do and store their docids/scores
#define NUM_RESULTS_FOR_RELATED_DOCIDS 300

// these are 1-1 with the top list of ptrs to Msg99Replies
class TopDocIds {
public:
	long      m_queryNum; // in the matchingquerybuf
	long      m_numDocIds;
	long long m_topDocIds[NUM_RESULTS_FOR_RELATED_DOCIDS];
	float     m_topScores[NUM_RESULTS_FOR_RELATED_DOCIDS];
	long      m_topSiteHashes26[NUM_RESULTS_FOR_RELATED_DOCIDS];
};


/*
class Msg99Request {
public:
	char  m_justGetQueryOffsets;
	char *ptr_twids;
	char *ptr_coll;
	char *ptr_posdbTermList;
	long  size_twids;
	long  size_coll;
	long  size_posdbTermList;
	char  m_buf[0];
};

class QueryInfo {
public:
	// stuff for setting importance
	short m_numUniqueWordForms;
	short m_numRepeatWordForms;
	short m_numControlWordForms;
	float m_smallestNormTermFreq;
	// hash of wids in query. see Query::getHash() function.
	long long m_queryExactHash64; 
	// hash of smallest syn of each wid in qry
	long long m_querySynBaseHash64; 
	// this is set by setQueryImportance()
	float m_queryImportance;
	// . how many docids are in our linked list, QueryRel::m_next
	// . only used by getRelatedQuery*() functions
	long  m_docIdVotes;
	// . score from combining all in linked list
	// . only used by getRelatedQuery*() functions
	float m_myScoreRelated;
};
*/

#define MAX_MATCHING_QUERIES 300
// only store this many of the top-scoring related docids
#define MAX_RELATED_DOCIDS 300
#define MAX_RELATED_QUERIES  300



// a related query must be shared by this many related docids in order
// to be scored. i had 640,000 related query
// candidates to score in getRelatedQueryBuf() for gigablast.com when this was 
// 10.
// there were a total of 3.8M related queries among all the related docids. so
// the MIN_DOCID_VOTES constraint is about 20% of the total.
#define MIN_DOCID_VOTES 10

// m_flags values
#define QEF_ESTIMATE_SCORE 0x01

// use this now to make things simpler
class QueryLogEntry {
public:
	char  m_qn;
	char  m_flags;
	uint8_t m_langId;
	long  m_gigablastTraffic;
	long  m_googleTraffic;
	long  m_googleTrafficDate;
	float m_topSERPScore; // score of the #1 result, in SLICE!
	float m_minTop50Score;
	long  m_minTop50ScoreDate;
	// how many results we got when we did this query on a slice
	// of the index. slice sizes should all be the same.
	long  m_numTotalResultsInSlice;
	unsigned long  m_queryTermIds32[0];
	//
	// NOTE: query string follows this list of termids
	//

	char *getQueryStr () {
		return (char *)((char *)this+sizeof(QueryLogEntry)+m_qn*4);};
	char *getQueryString () {
		return (char *)((char *)this+sizeof(QueryLogEntry)+m_qn*4);};

	uint8_t getQueryLangId () {
		// assume english if unknown (langUnknown = 0)
		if ( m_langId == langUnknown ) return langEnglish;
		return m_langId; 
	};
	
	long getSize() {
		char *start = (char *)this;
		char *p = start;
		p += sizeof(QueryLogEntry)+m_qn*4;
		p += gbstrlen(p) + 1;
		return p - start;
	};

};	

/*
// . this is basically a MATCHING QUERY
// . it could match our main url, or it could match a related docid; it is 
//   used for both
// . we get one of these back in response to a Msg99Request
// . we send the Msg99Requests out in batch in XmlDoc.cpp::sendBin()
// . it gives us a query that matches the termlists in Msg99Request which
//   are from our docid
class Msg99Reply {
public:
	// . just pass this whole thing back
	// . crap, but the query part is bogus and so are the
	//   query termids... because they are beyond the class's fixed size
	QueryLogEntry m_queryLogEntry;
	// estimated # searches the query gets per day
	//long  m_gigablastTraffic; 
	//long  m_googleTraffic;
	// offset into g_qbuf on that host that query string came from
	// NO! now it is to the full query entry which has the # of terms,
	// the pop and the termids followed by the string!
	long  m_qbufOffset;
	// replying host's hostid
	short m_replyingHostId;
	// was the query added from the m_extraQueryBuf which is set
	// from the user-supplied textarea
	char  m_isManuallyAdded:1; 
	//char  m_hasFullScore:1;
	// is it first in a linked list of msg99replies for the same query
	// but different m_myDocId? (for related queries algo)
	char  m_isFirst:1;
	// score of the query
	float     m_myScore;                
	// docid of related query, set after getting reply
	long long m_myDocId;
	// hmmm. what's this? the top 300 or so scoring docids, set by
	// getMatchingQueriesScoredForThisUrl() .
	//class TopDocIds *m_topDocIds;
	long m_topDocIdsBufOffset;

	TopDocIds *getTopDocIds ( SafeBuf *topDocIdsBuf ) {
		if ( m_topDocIdsBufOffset < 0 ) return NULL;
		char *p = topDocIdsBuf->getBufStart();
		p += m_topDocIdsBufOffset;
		return (TopDocIds *)p;
	};

	// this is set in handleRequest99() in seo.cpp
	//float m_minTop50Score;
	// this is also set in handleRequest99() in seo.cpp and used
	// in XmlDoc::getMatchingQueriesScoredForFullQuery()
	// for doing related docids, because we do not want to dedup
	// related queries if they are basically the same like
	// "search+engine" is too similar to "search+engines" so this
	// will allow us to skip them because they yield like the exact
	// same related docids!!!!
	long long m_querySynBaseHash64;
	// . how important is this query to the main url?
	// . now a function of m_numTotalResultsInSlice and query's traffic
	// . this is set when processing the msg99replies in 
	//   XmlDoc.cpp::getMatchingQueriesScoredForThisUrl()
	float m_queryImportance;
	// and the query string itself
	char  m_queryStr[0];
	
	long getSize() { return (long)sizeof(Msg99Reply)+
				 gbstrlen(m_queryStr)+
				 1; };
};
*/

// this is the term being inserted into a document. it may affect the
// document's ranking for multiple queries depending on what position it is
// inserted into. that ranking info should be described by the m_queryChanges
// array.
class InsertableTerm {
public:

	//
	// these members are set by getInsertableTerms():
	//

	//char *m_termStr;
	//long  m_termLen;
	// need this for matching to QueryChange::m_termHash64
	long long m_termHash64;
	// . sum of traffic of all queries that had this term
	// . maybe sort by this after m_bestTrafficGain?
	long  m_trafficSum;
	// is it a related term? i.e. from a related query as opposed to
	// a matching query. i.e. not contained by our doc..
	char m_isRelatedTerm:1;

	//
	// the following members are set by get*SCORED*InsertableTerms():
	//

	// first QueryChange in linked list, sorted by QueryChange::m_insertPos
	class QueryChange *m_firstQueryChange;
	// . this is only set in getInsertableTermsScore()
	// . indicates how much traffic we can gain by inserting this
	//   term into the document or a link
	long  m_bestTrafficGain;
	long  m_bestInsertPos;
	// the first QueryChange in the linked list for this m_bestWordPosition
	class QueryChange *m_bestQueryChange;

	// includes \0 terminating the term
	long m_termSize;
	// store the term string here
	char m_buf[0];

	char *getTerm ( ) { return m_buf; };
	long  getTermLen ( ) { return m_termSize - 1; };

	long getSize ( ) { return sizeof(InsertableTerm)+m_termSize; };
};

class RelatedDocId {
public:
	long long m_docId;
	// from clusterdb from doing a search:
	long m_siteHash26;
	// how many queries we have in common with the main url
	long      m_numCommonQueries;
	char      m_rd_siteRank;
	uint8_t   m_rd_langId;
	// the full site hash from titlerec, not just clusterdb!
	long      m_rd_siteHash32;
	// sum of the Msg99Reply::m_queryImportance for each query we have
	// in common with the main url.
	// m_queryImportance is the score this related docid had for
	// a matching query divided by a top score for that query.
	//float     m_similarityScore;//queryImportanceIntersectionSum;
	// . linked list of the QueryNums we have in common with main url
	// . these are offsets into m_commonQueryNumBuf safebuf
	long m_firstCommonQueryNumOff ;
	//long m_lastCommonQueryNumOff  ;
	long rd_title_off;
	long rd_url_off;
	long rd_site_off;

	char *getUrl ( class SafeBuf *relatedTitleBuf ) { 
		if ( rd_url_off == -1 ) return NULL;
		return relatedTitleBuf->getBufStart() + rd_url_off;
	};

	char *getSite ( class SafeBuf *relatedTitleBuf ) { 
		if ( rd_site_off == -1 ) return NULL;
		return relatedTitleBuf->getBufStart() + rd_site_off;
	};

	char *getTitle ( class SafeBuf *relatedTitleBuf ) { 
		if ( rd_title_off == -1 ) return NULL;
		return relatedTitleBuf->getBufStart() + rd_title_off;
	};


	// use offsets not ptrs! offsets into m_relatedTitleBuf safebuf
	long      m_linkInfo1Offset;
	// . the ip address of this m_docId
	// . actually, the ip address for the FIRST time we encountered this
	//   subdomain. so it may be old, but it is consistent.
	long      m_relatedFirstIp;
	long      m_relatedCurrentIp;

	// try just this
	// . A = vector of our scores for the queries we have
	//       in common with the main url
	// . B = vector of main url's scores for queries it has in common
	//       with this m_relatedDocId
	// . C = vector of the score of top result for such queries
	// . dotProduct = (A/C * B/C)
	// . so the relatedDocId that has the highest m_dotProduct
	//   is the most similar
	// . aka m_similarity!!!! but relative to all the other related docids
	//float m_dotProduct;

	// this replaces dot product
	float m_relatedWeight;

};

class RecommendedLink {
public:
	// sum of related docids' m_dotProduct values that have this inlink
	float m_totalRecommendedScore;
	// how many related docids have this inlink
	long  m_votes;
	// the siterank of this inlink
	char  m_rl_siteRank;
	// docid of the link
	long long m_rl_docId;
	long m_rl_firstIp;
	// offsets into XmlDoc::m_relatedDocIdBuf of the related docids
	// that this link links to
	long m_relatedDocIdOff[10];
	// these include the \0
	long m_urlSize;
	long m_titleSize;
	// offsets relative to "this" now! strange!!
	//long  m_urlOffset;
	//long  m_titleOffset;

	char *getUrl ( ) {
		char *ptr = (char *)this;
		ptr += sizeof(RecommendedLink);
		return ptr;
	};

	char *getTitle ( ) {
		char *ptr = (char *)this;
		ptr += sizeof(RecommendedLink);
		ptr += m_urlSize;
		return ptr;
	};

	long getSize() { return sizeof(RecommendedLink) + 
				 // these are stored right after us in buf
				 m_urlSize +
				 m_titleSize; };
};

// use for related urls/docids:
class QueryNumLinkedNode {
public:
	long m_queryNum;
	// offset of next link in linked list into m_commonQueryNumBuf
	long m_nextOff;
	// new stuff
	long m_relatedDocIdRank;
	float m_relatedDocIdSerpScore;
	long m_mainUrlRank;
	// the sum of these is the RelatedDocid::m_relatedWeight, the
	// final score of the related docid:
	float m_queryScoreWeight;
	//float m_mainUrlSerpScore;
};


// used by buffers returned by getMatchingQueryBuf() and getRelatedQueryBuf()
class QueryLink {
public:

	// offset to the corresponding QueryLogEntry
	long m_queryStringOffset;

	// score of this docid from gbdocid:xxx|querystr
	float m_serpScore;

	// . individial score for this QueryLink.
	// . different algo used to compute this for m_matchingQueryBuf 
	//   compared to m_relatedQueryBuf
	float m_queryImportance;

	// sum of all QueryLinks::m_importance that have this same
	// query/m_queryStringOffset. only valid for head of linked list
	// in the case of m_relatedQueryBuf.
	float m_totalQueryImportance;

	// a linked list of QueryLinks
	//long m_tailOff;
	//long m_nextOff;

	// now QueryLinks with the same m_queryStringOffset (query) are
	// stored all consecutively, sorted by their m_queryImportance.
	// m_isFirst is set to true for the first one in the list.
	// INCLUDES the head querylink!!!!
	long m_numInList;

	// how many related docIds contributed to m_totalRelatedQueryImportance
	short m_docIdVotes;

	// . the docid that had this query as a matching query
	// . HACK: if this QueryLink is for a matching query of the main url
	//   then we use this for the topdocids # in m_topDocIdBuf
	short m_relatedDocIdNum;

	// a flag. the head of the linked list?
	char  m_isFirst:1;

	// . sort by this first then by m_totalRelatedQueryImportance
	// . the lower this value, the higher this query will be displayed
	uint8_t m_uniqueRound;


	//long long getQueryLinkHash64 ( ) {
	//	long long h64 = m_queryHostId;
	//	h64 <<= 32;
	//	h64 |= m_queryDataOffset;
	//	return h64;
	//};

	class RelatedDocId *getRelatedDocId ( SafeBuf *relatedDocIdBuf ) {
		if ( m_relatedDocIdNum == -1 ) return NULL;
		RelatedDocId *rds ;
		rds = (RelatedDocId *)relatedDocIdBuf->getBufStart();
		return &rds[m_relatedDocIdNum];
	};

	/*
	class QueryLink *getNext ( SafeBuf *queryLinkBuf ) {
		if ( m_nextOff == -1 ) return NULL;
		char *base = queryLinkBuf->getBufStart();
		return (QueryLink *)(base + m_nextOff);
	};

	class QueryLink *getTail ( SafeBuf *queryLinkBuf ) {
		if ( m_tailOff == -1 ) return NULL;
		char *base = queryLinkBuf->getBufStart();
		return (QueryLink *)(base + m_tailOff);
	};
	*/

	class QueryLogEntry *getQueryLogEntry ( SafeBuf *stringBuf) {
		char *base = stringBuf->getBufStart();
		QueryLogEntry *qe;
		qe = (QueryLogEntry *)(base + m_queryStringOffset);
		return qe;
	};

	// these are in string buf, m_stringBuf too!
	float getTopOfSliceSERPScore ( SafeBuf *stringBuf ) {
		QueryLogEntry *qe = getQueryLogEntry(stringBuf);
		return qe->m_topSERPScore;
	};
	long getGigablastTraffic ( SafeBuf *stringBuf ) {
		QueryLogEntry *qe = getQueryLogEntry(stringBuf);
		return qe->m_gigablastTraffic;
	};
	// this is -1 if unknown
	long getGoogleTraffic ( SafeBuf *stringBuf ) {
		QueryLogEntry *qe = getQueryLogEntry(stringBuf);
		return qe->m_googleTraffic;
	};
	char *getQueryString ( SafeBuf *stringBuf ) {
		QueryLogEntry *qe = getQueryLogEntry(stringBuf);
		return qe->getQueryString();
	};

};


class MissingTerm {
public:
	// how many related docids had this term
	long m_votes;
	// what is the score
	float m_importance;//score;

	// sum of traffic of all related queries that had this term
	long long m_traffic;

	// linked list of synonyms
	//class MissingTerm *m_synNext;
	//class MissingTerm *m_synTail;
	// what missing term are we a synonym of?
	class MissingTerm *m_synOf;
	// we get the largest phrase in wikipedia title's to make a full phrase
	// like "modern warfare 3" etc...
	//long m_numAlnumWords;

	char m_isMissingTerm;
	char m_reserved2;
	char m_reserved3;
	char m_reserved4;

	// . the first ten related queries that contain this
	// . they are offsets to QueryLogEntries
	// . use -1 to indicate end...
	// . if m_isFromRelatedQuery is true these are offsets are for
	//   a QueryLogEntry in the m_relatedQueryDataBuf, 
	//   OTHERWISE these are offsets into
	//   the m_msg99ReplyBuf and a msg99Reply
	long m_hackQueryOffsets[10];

	// . get ith query that contains this term
	// . bbb is relatedQueryDataBuf for related terms
	// . bbb is m_msg99ReplyBuf for matching terms
	char *getContainingQuery ( long i , class XmlDoc *xd ) ;

	char *getTerm() { return m_buf; };

	long getTermSize() { return m_termSize; };
	long getTermLen() { return m_termSize-1; };

	long  getSize () { return sizeof(MissingTerm)+m_termSize;};

	long m_termSize;
	char m_buf[0];
};


#endif
