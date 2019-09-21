// Matt Wells, copyright Aug 2003

// Query is a class for parsing queries

#ifndef _QUERY_H_
#define _QUERY_H_

#include "SafeBuf.h"
#include "Mem.h"

// keep these down to save memory
//#define MAX_QUERY_LEN   8000 // url:XXX can be quite long! (MAX_URL_LEN)
//#define MAX_QUERY_LEN 3200
// support big OR queries for image shingles
#define ABS_MAX_QUERY_LEN 62000
// . words need to deal with int32_t list of sites!
// . remember, words can be string of punctuation, too
//#define MAX_QUERY_WORDS 5000 
//#define MAX_QUERY_WORDS 32000 
// not any more!
//#define MAX_QUERY_WORDS 320 
// raise for crazy bool query on diffbot
// seems like we alloc just enough to hold our words now so that this
// is really a performance capper but it is used in Summary.cpp
// and Matches.h so don't go too big just yet
//#define MAX_QUERY_WORDS 800
#define ABS_MAX_QUERY_WORDS 99000

// . how many IndexLists might we get/intersect
// . we now use a int64_t to hold the query term bits for non-boolean queries
//#define MAX_QUERY_TERMS 216
//#define MAX_QUERY_TERMS 512
// seems like CTS is causing huge delay spiders in query processing so
// truncate for now...
//#define MAX_QUERY_TERMS 40
// we need more for zak's categories!
//#define MAX_QUERY_TERMS 1500
// nah, do 40 again
//#define MAX_QUERY_TERMS 40
// how to make a lock pick set loses synonyms from 40!
//#define MAX_QUERY_TERMS 80
//#define MAX_QUERY_TERMS 160
#define ABS_MAX_QUERY_TERMS 9000

// only allow up to 200 interests from facebook plus manually entered
// because we are limited by the query terms above so we can only
// UOR so many in SearchInput.cpp
#define MAX_INTERESTS 200

#define GBUF_SIZE (16*1024)
#define SYNBUF_SIZE (16*1024)

// score of highest-scoring query term in the QueryScore
//#define BASE_QUERY_SCORE 10000000

// let's support up to 64 query terms for now
typedef uint64_t qvec_t;

#define MAX_EXPLICIT_BITS (sizeof(qvec_t)*8)

#define MAX_OVEC_SIZE 256

// only can use 16-bit since have to make a 64k truth table!
#define MAX_EXPLICIT_BITS_BOOLEAN (16*8)

// field codes
#define FIELD_URL      1
#define FIELD_LINK     2
#define FIELD_SITE     3
#define FIELD_IP       4
#define FIELD_SUBURL   5
#define FIELD_TITLE    6
#define FIELD_TYPE     7
#define FIELD_EXT      21
#define FIELD_COLL     22
#define FIELD_ILINK    23
#define FIELD_LINKS    24
#define FIELD_SITELINK 25
// non-standard field codes
#define FIELD_ZIP      8
#define FIELD_CITY     9
#define FIELD_STREET   10
#define FIELD_AUTHOR   11
#define FIELD_LANG     12
#define FIELD_CLASS    13
#define FIELD_COUNTRY  14
#define FIELD_TAG      15
#define FIELD_STATE    16
#define FIELD_DATE     17
#define FIELD_GENERIC  18
#define FIELD_ISCLEAN  19  // we hash field="isclean:" val="1" if doc clean
//#define FIELD_RANGE    20  // date range OBSOLETE, was only for newspaperarchive
#define FIELD_CHARSET  30
#define FIELD_GBRSS    31
#define FIELD_URLHASH       32
#define FIELD_URLHASHDIV10  33
#define FIELD_URLHASHDIV100 34
#define FIELD_GBRULESET     35
#define FIELD_GBLANG        36
#define FIELD_GBQUALITY     37
#define FIELD_LINKTEXTIN    38
#define FIELD_LINKTEXTOUT   39
#define FIELD_KEYWORD       40
#define FIELD_QUOTA            41
#define FIELD_GBTAGVECTOR      42
#define FIELD_GBGIGABITVECTOR  43
#define FIELD_GBSAMPLEVECTOR   44
#define FIELD_SYNONYM          45
#define FIELD_GBCOUNTRY        46
#define FIELD_GBAD             47
#define FIELD_GBSUBMITURL      48

#define FIELD_GBPERMALINK      49
#define FIELD_GBCSENUM         50
#define FIELD_GBSECTIONHASH    51
#define FIELD_GBDOCID          52
#define FIELD_GBCONTENTHASH    53 // for deduping at spider time
#define FIELD_GBSORTBYFLOAT    54 // i.e. sortby:price -> numeric termlist
#define FIELD_GBREVSORTBYFLOAT 55 // i.e. sortby:price -> low to high
#define FIELD_GBNUMBERMIN      56
#define FIELD_GBNUMBERMAX      57
#define FIELD_GBPARENTURL      58

#define FIELD_GBSORTBYINT      59
#define FIELD_GBREVSORTBYINT   60
#define FIELD_GBNUMBERMININT   61
#define FIELD_GBNUMBERMAXINT   62
#define FIELD_GBFACETSTR       63
#define FIELD_GBFACETINT       64
#define FIELD_GBFACETFLOAT     65
#define FIELD_GBNUMBEREQUALINT 66
#define FIELD_GBNUMBEREQUALFLOAT 67
#define FIELD_SUBURL2            68
#define FIELD_GBFIELDMATCH       69

#define FIELD_GBOTHER 92

// returns a FIELD_* code above, or FIELD_GENERIC if not in the list
char getFieldCode  ( char *s , int32_t len , bool *hasColon = NULL ) ;
char getFieldCode2 ( char *s , int32_t len , bool *hasColon = NULL ) ;
char getFieldCode3 ( int64_t h64 ) ;

int32_t getNumFieldCodes ( );

// . values for QueryField::m_flag
// . QTF_DUP means it is just for the help page in PageRoot.cpp to 
//   illustrate a second or third example
#define QTF_DUP  0x01
#define QTF_HIDE 0x02
#define QTF_BEGINNEWTABLE 0x04

struct QueryField {
	char *text;
	char field;
	bool hasColon;
	char *example;
	char *desc;
	char *m_title;
	char  m_flag;
};

extern struct QueryField g_fields[];
	
// reasons why we ignore a particular QueryWord's word or phrase
#define IGNORE_DEFAULT   1 // punct
#define IGNORE_CONNECTED 2 // connected sequence (cd-rom)
#define IGNORE_QSTOP     3 // query stop word (come 'to' me)
#define IGNORE_REPEAT    4 // repeated term (time after time)
#define IGNORE_FIELDNAME 5 // word is a field name, like title:
#define IGNORE_BREECH    6 // query exceeded MAX_QUERY_TERMS so we ignored part
#define IGNORE_BOOLOP    7 // boolean operator (OR,AND,NOT)
#define IGNORE_QUOTED    8 // word in quotes is ignored. "the day"
//#define IGNORE_SYNONYM   9 // part of a gbsynonym: field

// . reasons why we ignore a QueryTerm
// . we replace sequences of UOR'd terms with a compound term, which is
//   created by merging the termlists of the UOR'd terms together. We store
//   this compound termlist into a cache to avoid having to do the merge again.
#define IGNORE_COMPONENT 9 // if term was replaced by a compound term

// boolean query operators (m_opcode field in QueryWord)
#define OP_OR         1
#define OP_AND        2
#define OP_NOT        3
#define OP_LEFTPAREN  4
#define OP_RIGHTPAREN 5
#define OP_UOR        6
#define OP_PIPE       7

////////////////////////////////////////////////////////
////////////////////////////////////////////////////////
//////////        BEGIN BOOLEAN STUFF      /////////////
////////////////////////////////////////////////////////
////////////////////////////////////////////////////////

/*
// . creating a QueryBoolean class was unnecessary since it was only functional
//   and had nothing new it would store that the Query class doesn't store
// . the entry point is the Query::setBitScoresBoolean() function below

// . we can have as many operands (plain opds, not expressions) as query terms
// . no, not anymore, we boosted MAX_QUERY_TERMS so we can have UORs which
//   essentially make a bunch of terms use the same explicit bit
//#define MAX_OPERANDS 16
#define MAX_OPERANDS (MAX_QUERY_TERMS)

class Operand {
public:
	int32_t set ( int32_t a , int32_t b , class QueryWord *qwords , int32_t level ,
		   bool underNOT ) ;
	// . "bits" are 1-1 with the query terms in Query::m_qterms[] array
	// . Operand::m_opBits is the required bits for operand to be true
	// . does not include signless phrases
	//bool isTruth ( qvec_t bits, qvec_t mask=(qvec_t)-1 ) {
	bool isTruth ( unsigned char *bitVec , int32_t vecSize ) {
		// must always satisfy hard required terms (+ sign)
		//if ( (bits & m_forcedBits) != m_forcedBits )
		//	return false;
		//if (m_hasNOT) return (bits & m_opBits & mask) == 0;
		//return ( (bits & m_opBits & mask) == (m_opBits & mask)); 
		if ( m_hasNOT ) {
			for ( int32_t i = 0 ; i < vecSize ; i++ )
				if ( m_opBits[i] & bitVec[i] ) return false;
			return true;
		}
		for ( int32_t i = 0 ; i < vecSize ; i++ )
			if ( m_opBits[i] & bitVec[i] ) return true;
		return false;
		// . we are now back to good ol' default OR
		// . m_opBits should have been masked with
		//   m_requiredBits so as not to include signless phrases
		//return ( (bits & m_opBits) != 0 ); 
	};
	void print (SafeBuf *sbuf);
	// we are a sequence of QueryWords
	//int32_t m_startWordNum;
	//int32_t m_lastWordNum;
	// . doc just needs one of these bits for this op to be considered true
	// . terms under the same QueryTermInfo class should have the same
	//   termbit here
	unsigned char m_opBits[MAX_OVEC_SIZE];
	//int32_t m_vecSize;
	// does the word NOT preceed the operand?
	bool   m_hasNOT;
	//class Expression *m_parent;

	// we MUST have these for this OPERAND to be true
	//uint16_t m_forcedBits;
};
*/


////////////////////////////////////////////////////////
////////////////////////////////////////////////////////
//////////         END BOOLEAN STUFF       /////////////
////////////////////////////////////////////////////////
////////////////////////////////////////////////////////


#define MAX_FACET_RANGES 256

// . these first two classes are functionless
// . QueryWord, like the Phrases class, is an extension on the Words class
// . the array of QueryWords, m_qwords[], is contained in the Query class
// . we compute the QueryTerms (m_qterms[]) from the QueryWords
class QueryWord {

 public:
	bool isAlphaWord() { return is_alnum_utf8(m_word); };
	bool hasWhiteSpace() { 
		char *p = m_word;
		char *pend = m_word + m_wordLen;
		for ( ; p < pend ; p += getUtf8CharSize ( p ) )
			if ( is_wspace_utf8 ( p ) ) return true;
		return false;
	};
	void constructor ();
	void destructor ();

	//UCScript wordScript() { 
	//	UChar*foo;
	//	return ucGetScript(utf16Decode((UChar*)(m_word),&foo));
	//}

	// this ptr references into the actual query
	char       *m_word    ;
	int32_t        m_wordLen ;
	// the length of the phrase, if any. it starts at m_word
	int32_t        m_phraseLen;
	// this is the term hash with collection and field name and
	// can be looked up directly in indexdb
	int64_t   m_wordId ;
	int64_t   m_phraseId;
	// hash of field name then collection, used to hash termId
	int64_t   m_prefixHash;
	int32_t        m_wordNum;
	int32_t        m_posNum;
	// are we in a phrase in a wikipedia title?
	int32_t        m_wikiPhraseId;
	int32_t        m_wikiPhraseStart;
	int32_t        m_numWordsInWikiPhrase;

	// . this is just the hash of m_term and is used for highlighting, etc.
	// . it is 0 for terms in a field?
	int64_t   m_rawWordId ;
	int64_t   m_rawPhraseId ;
	// if we are phrase, the end word's raw id
	int64_t   m_rightRawWordId;
	// the field as a convenient numeric code
	char        m_fieldCode ;
	// . '-' means to exclude from search results
	// . '+' means to include in all search results
	// . if we're a phrase term, signs distribute across quotes
	char        m_wordSign;
	char        m_phraseSign;
	// this is 1 if the associated word is a valid query term but its
	// m_explicitBit is 0. we use this to save explicit bits for those
	// terms that need them (like those terms in complicated nested boolean
	// expressions) and just use a hardCount to see how many hard required
	// terms are contained by a document. see IndexTable.cpp "hardCount"
	char        m_hardCount;
	// the parenthetical level of this word in the boolean expression.
	// level 0 is the first level.
	char        m_level;
	// . how many plusses preceed this query term?
	// . the more plusses the more weight it is given
	//char        m_numPlusses ;
	// is this word a query stop word?
	bool        m_isQueryStopWord ; 
	// is it a plain stop word?
	bool        m_isStopWord ; 
	bool        m_isPunct;
	// are we an op code?
	char        m_opcode;
	// . the ignore code
	// . explains why this query term should be ignored
	// . see #define'd IGNORE_* codes above
	char        m_ignoreWord   ;
	char        m_ignorePhrase ;

	// so we ignore gbsortby:offerprice in bool expressions
	char        m_ignoreWordInBoolQuery;

	// is this query single word in quotes?
	bool        m_inQuotes ; 
	// is this word in a phrase that is quoted?
	bool        m_inQuotedPhrase;
	// what word # does the quote we are in start at?
	int32_t        m_quoteStart;
	int32_t        m_quoteEnd; // inclusive!
	// are we connected to the alnum word on our left/right?
	bool        m_leftConnected;
	bool        m_rightConnected;
	// if we're in middle or right end of a phrase, where does it start?
	int32_t        m_leftPhraseStart;
	// . what QueryTerm does our "phrase" map to? NULL if none.
	// . this allows us to OR in extra bits into that QueryTerm's m_bits
	//   member that correspond to the single word constituents
	// . remember, m_bits is a bit vector that represents the QueryTerms
	//   a document contains
	class QueryTerm *m_queryPhraseTerm;
	// . what QueryTerm does our "word" map to? NULL if none.
	// . used by QueryBoolean since it uses QueryWords heavily
	class QueryTerm *m_queryWordTerm;
	// user defined weights

	int32_t m_userWeight;
	char m_userType;
	float m_userWeightPhrase;

	char m_userTypePhrase;
	bool m_queryOp;
	// is it after a NOT operator? i.e. NOT ( x UOR y UOR ... )
	bool m_underNOT;
	// is this query word before a | (pipe) operator?
	bool m_piped;
	// used by Matches.cpp for highlighting under different colors
	int32_t m_colorNum;

	// for min/max score ranges like gbmin:price:1.99
	float m_float;
	// for gbminint:99 etc. uses integers instead of floats for better res
	int32_t  m_int;

	// for holding some synonyms
	SafeBuf m_synWordBuf;


	int32_t  m_facetRangeIntA   [MAX_FACET_RANGES];
	int32_t  m_facetRangeIntB   [MAX_FACET_RANGES];
	float m_facetRangeFloatA [MAX_FACET_RANGES];
	float m_facetRangeFloatB [MAX_FACET_RANGES];
	int32_t  m_numFacetRanges;


	// what operand bit # is it for doing boolen queries?
	//int32_t  m_opBitNum;
	// when an operand is an expression...
	class Expression *m_expressionPtr;
};

// . we filter the QueryWords and turn them into QueryTerms
// . QueryTerms are the important parts of the QueryWords
class QueryTerm {

 public:

	//QueryTerm ( ) { constructor(); };

	void constructor ( ) ;

	// the query word we were derived from
	QueryWord *m_qword;
	// . are we a phrase termid or single word termid from that QueryWord?
	// . the QueryWord instance represents both, so we must choose
	bool       m_isPhrase;
	// for compound phrases like, "cat dog fish" we do not want docs
	// with "cat dog" and "dog fish" to match, so we extended our hackfix
	// in Summary.cpp to use m_phrasePart to do this post-query filtering
	int32_t       m_phrasePart;
	// this is phraseId for phrases, and wordId for words
	int64_t  m_termId;
	// used by Matches.cpp
	int64_t  m_rawTermId;

	// . if we are a phrase these are the termids of the word that
	//   starts the phrase and the word that ends the phrase respectively
	int64_t  m_rightRawWordId;
	int64_t  m_leftRawWordId;

	// sign of the phrase or word we used
	char       m_termSign;
	// our representative bit (up to 16 MAX_QUERY_TERMS)
	//uint16_t m_explicitBit;
	qvec_t     m_explicitBit;
	// usually this equal m_explicitBit, BUT if a word is repeated
	// in different areas of the doc, we union all the individual
	// explicit bits of that repeated word into this bit vec. it is
	// used by Matches.cpp only so far.
	qvec_t     m_matchesExplicitBits;
	// this is 1 if the associated word is a valid query term but its
	// m_explicitBit is 0. we use this to save explicit bits for those
	// terms that need them (like those terms in complicated nested boolean
	// expressions) and just use a hardCount to see how many hard required
	// terms are contained by a document. see IndexTable.cpp "hardCount"
	char       m_hardCount;

	// the "number" of the query term used for evaluation boolean
	// expressions in Expression::isTruth(). Basically just the
	// QueryTermInfo for which this query term belongs. each QueryTermInfo
	// is like a single query term and all its synonyms, etc.
	int32_t       m_bitNum;

	// point to term, either m_word or m_phrase
	char      *m_term;
	int32_t       m_termLen;
	// point to the posdblist that represents us
	class RdbList   *m_posdbListPtr;

	// languages query term is in. currently this is only valid for
	// synonyms of other query terms. so we can show what language the
	// synonym is for in the xml/json feed.
	uint64_t m_langIdBits;
	bool m_langIdBitsValid;

	// the ()'s following an int/float facet term dictate the
	// ranges for clustering the numeric values. like 
	// gbfacetfloat:price:(0-10,10-20,...)
	// values outside the ranges will be ignored
	char *m_parenList;
	int32_t  m_parenListLen;

	int32_t   m_componentCode;
	int64_t   m_termFreq;
	float     m_termFreqWeight;

	// . our representative bits
	// . the bits in this bit vector is 1-1 with the QueryTerms
	// . if a doc has query term #i then bit #i will be set
	// . if a doc EXplicitly has phrase "A B" then it may have 
	//   term A and term B implicity
	// . therefore we also OR the bits for term A and B into m_implicitBits
	// . THIS SHIT SHOULD be just used in setBitScores() !!!
	//uint16_t m_implicitBits;
	qvec_t m_implicitBits;
	// Summary.cpp and Matches.cpp use this one
	bool m_isQueryStopWord ; 
	// IndexTable.cpp uses this one
	bool m_inQuotes;
	// . is this term under the influence of a boolean NOT operator?
	// . used in IndexReadInfo.cpp, if so we must read the WHOLE termlist
	bool m_underNOT;
	// is it a repeat?
	char m_repeat;
	// user defined weight for this term, be it phrase or word
	float m_userWeight;
	char m_userType;
	// . is this query term before a | (pipe) operator?
	// . if so we must read the whole termlist, like m_underNOT above
	bool m_piped;
	// . we ignore component terms unless their compound term is not cached
	// . now this is used to ignore low tf synonym terms only
	char m_ignored ;
	// is it part of a UOR chain?
	bool m_isUORed;
	QueryTerm *m_UORedTerm;
	// . if synonymOf is not NULL, then m_term points into m_synBuf, not
	//   m_buf
	//int32_t m_affinity; 	// affinity to the synonym
	QueryTerm *m_synonymOf;
	int64_t m_synWids0;
	int64_t m_synWids1;
	int32_t      m_numAlnumWordsInSynonym;
	// like if we are the "nj" syn of "new jersey", this will be 2 words
	// since "new jersey", our base, is 2 alnum words.
	int32_t      m_numAlnumWordsInBase;
	// the phrase affinity from the wikititles.txt file used in Wiki.cpp
	//float m_wikiAff ;
	// if later, after getting a more accurate term freq because we 
	// actually download the termlist, its term freq drops a lot, we may
	// end up filtering it in Query::filterSynonyms() called by Msg39. in
	// which case the termlist is reset to 0 so it does not play a role
	// in the search results computations in IndexTable2.cpp.
	//char m_isFilteredSynonym;
	// copied from derived QueryWord
	char m_fieldCode  ;
	bool isSplit();
	// . weights and affinities calculated in IndexTable2
	// . do not store in here, just pass along as a separate vector
	// . analogous to how Phrases is to Words is to Bits, etc.
	//float m_termWeight;
	//float m_phraseAffinity;
	bool m_isRequired;
	// . true if we are a word IN a phrase
	// . used by IndexTable2's getWeightedScore()
	char  m_inPhrase;
	char  m_isWikiHalfStopBigram:1;
	// if a single word term, what are the term #'s of the 2 phrases
	// we can be in? uses -1 to indicate none.
	int32_t  m_leftPhraseTermNum;
	int32_t  m_rightPhraseTermNum;
	// . what operand # are we a part of in a boolean query?
	// . like for (x AND y) x would have an opNum of 0 and y an
	//   opNum of 1 for instance.
	// . for things like (x1 OR x2 OR x3 ... ) we try to give all
	//   those query terms the same m_opNum for efficiency since
	//   they all have the same effecct
	//int32_t  m_opNum;
	
	// same as above basically
	class QueryTerm *m_leftPhraseTerm;
	class QueryTerm *m_rightPhraseTerm;
	// for scoring summary sentences from XmlDoc::getEventSummary()
	float m_score;

	// a queryTermInfo class is multiple "related"/synonym terms.
	// we have an array of these we set in Posdb.cpp:setQueryTermInfo().
	int m_queryTermInfoNum;

	// facet support in Posdb.cpp for compiling the data and we'll
	// send this back via Msg39Reply::ptr_facetHashList which will be
	// 1-1 with the query terms.
	HashTableX m_facetHashTable;

	// for sorting the facetEntries in m_facetHashTable
	SafeBuf m_facetIndexBuf;

	char m_startKey[MAX_KEY_BYTES];
	char m_endKey  [MAX_KEY_BYTES];
	char m_ks;

	// used by Msg40.cpp for gigabits generation
	int64_t m_hash64d;
	int32_t      m_popWeight;

	uint64_t m_numDocsThatHaveFacet;
};

//#define MAX_OPSLOTS 256

#define MAX_EXPRESSIONS 100

// operand1 AND operand2 OR  ...
// operand1 OR  operand2 AND ...
class Expression {
public:
	bool addExpression (int32_t start, 
			    int32_t end, 
			    class Query      *q,
			    int32_t    level );
	bool isTruth ( unsigned char *bitVec , int32_t vecSize );
	// . what QueryTerms are UNDER the influence of the NOT opcode?
	// . we read in the WHOLE termlist of those that are (like '-' sign)
	// . returned bit vector is 1-1 with m_qterms in Query class
	void print (SafeBuf *sbuf);
	// . a list of operands separated by op codes (a AND b OR c ...)
	// . sometimes and operand is another expression: a AND (b OR c)
	// . use NULL in m_operands slot if we got an expression and vice versa
	// . m_opcodes[i] is the opcode after operand #i
	//class Expression *m_parent;
	//bool              m_hasNOT;
	//int32_t              m_start;
	//int32_t              m_end;
	bool m_hadOpCode;
	int32_t m_expressionStartWord;
	int32_t m_numWordsInExpression;
	Query *m_q;
	// . opSlots can be operands operators or expressions
	// . m_opTypes tells which of the 3 they are
	//int32_t m_opSlots[MAX_OPSLOTS];
	//char m_opTypes[MAX_OPSLOTS];
	//int32_t m_cc;
};

// . this is the main class for representing a query
// . it contains array of QueryWords (m_qwords[]) and QueryTerms (m_qterms[])
class Query {

 public:
	void reset();

	Query();
	~Query();
	void constructor();
	void destructor();

	// . returns false and sets g_errno on error
	// . after calling this you can call functions below
	// . if boolFlag is 0 we ignore all boolean operators
	// . if boolFlag is 1  we assume query is boolen
	// . if boolFlag is 2  we attempt to detect if query is boolean or not
	bool set2 ( char *query    , 
		    //int32_t  queryLen , 
		    //char *coll     , 
		    //int32_t  collLen  ,
		    uint8_t  langId ,
		    char     queryExpansion ,
		    bool     useQueryStopWords = true ,
		    //char  boolFlag = 2 , // auto-detect if boolean query
		    //bool  keepAllSingles = false ,
		    int32_t  maxQueryTerms = 0x7fffffff );

	// serialize/deserialize ourselves so we don't have to pass the
	// unmodified string around and reparse it every time
	int32_t    getStoredSize();
	int32_t 	serialize(char *buf, int32_t bufLen);
	int32_t	deserialize(char *buf, int32_t bufLen);

	// . if a term is truncated in indexdb, change its '+' sign to a '*'
	// . will recopmute m_bitScores to fix bit #7
	//void softenTruncatedTerms ( );

	bool setQueryTermScores ( int64_t *termFreqsArg ) ;

	// about how hits for this query?
	//int64_t getEstimatedTotalHits ( );

	char *getQuery    ( ) { return m_orig  ; };
	int32_t  getQueryLen ( ) { return m_origLen; };

	//int32_t  getNumIgnored    ( ) { return m_numIgnored; };
	//int32_t  getNumNotIgnored ( ) { return m_numTerms ;  };

	int32_t       getNumTerms  (        ) { return m_numTerms;              };
	char       getTermSign  ( int32_t i ) { return m_qterms[i].m_termSign;  };
	bool       isPhrase     ( int32_t i ) { return m_qterms[i].m_isPhrase;  };
	bool       isInPhrase   ( int32_t i ) { return m_qterms[i].m_inPhrase;  };
	bool       isInQuotes   ( int32_t i ) { return m_qterms[i].m_inQuotes;  };
	int64_t  getTermId    ( int32_t i ) { return m_qterms[i].m_termId;    };
	char       getFieldCode2( int32_t i ) { return m_qterms[i].m_fieldCode; };
	int64_t  getRawTermId ( int32_t i ) { return m_qterms[i].m_rawTermId; };
	char      *getTerm      ( int32_t i ) { return m_qterms[i].m_term; };
	int32_t       getTermLen   ( int32_t i ) { return m_qterms[i].m_termLen; };
	bool       isQueryStopWord (int32_t i ) { 
		return m_qterms[i].m_isQueryStopWord; };
	// . not HARD required, but is term #i used for an EXACT match?
	// . this includes negatives and phrases with signs in addition to
	//   the standard signless single word query term
	bool       isRequired ( int32_t i ) { 
		if ( ! m_qterms[i].m_isPhrase ) return true;
		if (   m_qterms[i].m_termSign ) return true;
		return false;
	};

	//int32_t getNumRequired ( ) ;
	bool isSplit();

	bool isSplit(int32_t i) { return m_qterms[i].isSplit(); };

	// . Msg39 calls this to get our vector so it can pass it to Msg37
	// . the signs and ids are dupped in the QueryTerm classes, too
	//int64_t *getTermFreqs ( ) { return m_termFreqs ; };
	//int64_t  getTermFreq  ( int32_t i ) { return m_termFreqs[i]; };
	//int64_t *getTermIds   ( ) { return m_termIds   ; };
	//char      *getTermSigns ( ) { return m_termSigns ; };
	//int32_t      *getComponentCodes   ( ) { return m_componentCodes; };
	int64_t  getRawWordId ( int32_t i ) { return m_qwords[i].m_rawWordId;};

	int32_t getNumComponentTerms ( ) { return m_numComponents; };

	// sets m_bmap[][] so getImplicits() works
	void setBitMap ( );
	bool testBoolean(unsigned char *bits,int32_t vecSize);
	// print to log
	void printBooleanTree();
	void printQueryTerms();

	// the new way as of 3/12/2014. just determine if matches the bool
	// query or not. let's try to offload the scoring logic to other places
	// if possible.
	// bitVec is all the QueryWord::m_opBits some docid contains, so
	// does it match our boolean query or not?
	bool matchesBoolQuery ( unsigned char *bitVec , int32_t vecSize ) ;


	// . call this before calling getBitScore() to set m_bitScores[] table
	// . returns false and sets g_errno on error (ENOMEM usually)
	//bool setBitScores (qvec_t bitMask = (qvec_t)-1);

	// . m_bitScores[ BITS ] maps BITS to a bitScore
	// . the BITS of a doc is 1-1 with m_qterms[] present in that doc
	// . bitScore returns # of required terms implicitly in the doc 
	// . required terms do not include query terms from signless phrases
	// . if bitScore 0x80 is set it matches all forced terms (plus signs)
	// . if bitScore 0x40 is set it has all required terms EXplicitly
	// . if bitScore 0x20 is set it has all required terms IMplicitly
	// . example query: 'cat dog' --> "cat dog"=bit#0, cat=bit#1, dog=bit#2
	// . if a doc does not explicitly have 'cat', but it has the phrase 
	//   "cat dog" then it is said to have 'cat' implicitly... implied 
	//   through the phrase
	// . the greater the number of IMplicit SINGLE words a doc has the 
	//   bigger its bit score
	/*
	uint8_t getBitScore ( qvec_t ebits ) {
		// get implicit bits from explicit bits
		qvec_t ibits = getImplicits ( ebits );
		// . boolean queries are limited in the # of terms so that
		//   ebits should NOT be too big, under 10MB i think now
		// . sets the usual 0x80,0x40,0x20 + require termcount.
		// . for boolean queries, if we have too many 
		//   explicits then when we alloc for "need4" above, it
		//   should return ENOMEM and we should never make it 
		//   here! so "ibits" should not be too big and breach 
		//   the array!
		if ( m_isBoolean ) {
			//return m_bitScores[(uint32_t)ibits];
			uint8_t bscore = 0;
			if( testBoolean(ibits)) bscore = 0x80|0x40|0x20;
			return bscore;
		}
		// just get those required
		ibits &= m_requiredBits;
		// get the vector of required bits we implicitly have
		uint8_t *iv = (uint8_t *)&ibits;
		// set this
		uint8_t bscore ;
		// . how many terms we do have implicitly?
		// . the g_a table is in Mem.cpp and maps a byte to the number
		//   of bits it has that are in the ON position
		bscore = g_a[iv[0]] + g_a[iv[1]] + g_a[iv[2]] + g_a[iv[3]]
			+ g_a[iv[4]] + g_a[iv[5]] + g_a[iv[6]] + g_a[iv[7]];

		// if we have synonyms, then any implied bits a synonym has 
		// should be treated as explicit bits for "bit score" purposes
		// so that if someone searches 'cincinnati, oh' a doc with 
		// 'cincinnati ohio' is treated no lesser than a doc with 
		// 'cincinnati oh'. BUT only do this for "stem" or 
		// "morphological form" synonyms, because it is allows 'bib'
		// for the query 'michael bibby facts' to outscore docs that
		// have all the original query terms explicitly... so limit
		// it to just the "stem" synonyms. BUT, if the syn affinity
		// is 0, do not include these at all...
		//qvec_t ebits2=ebits | getImplicits(ebits&m_synonymBitsHiAff);
		qvec_t ebits2 = ebits | getImplicits(ebits&m_synonymBits);

		// then OR in some high bits
		if ((ebits2 & m_forcedBits)   == m_forcedBits   ) bscore|=0x80;
		if ((ebits2 & m_requiredBits) == m_requiredBits ) bscore|=0x40;
		if (ibits                     == m_requiredBits ) bscore|=0x20;
		return bscore;
	};
	*/

	// return an implicit vector from an explicit which contains the explic
	qvec_t getImplicits ( qvec_t ebits ) {
		if ( ! m_bmapIsSet ) { char *xx=NULL;*xx=0; }
		uint8_t *ev = (uint8_t *)&ebits;
		return	m_bmap[0][ev[0]] | 
			m_bmap[1][ev[1]] | 
			m_bmap[2][ev[2]] | 
			m_bmap[3][ev[3]] | 
			m_bmap[4][ev[4]] | 
			m_bmap[5][ev[5]] | 
			m_bmap[6][ev[6]] | 
			m_bmap[7][ev[7]] ;
	};

	//qvec_t getImplicitBitsFromTermNum ( int32_t qtnum ) {
	//};

	// returns false if no truths possible
	bool setBitScoresBoolean ( char *buf , int32_t bufSize );

	// ALWAYS call this after calling setBitScores(), it uses m_bitScores[]
	//int64_t getEstimatedTotalHitsBoolean ( );

	// sets m_qwords[] array, this function is the heart of the class
	bool setQWords ( char boolFlag , bool keepAllSingles ,
			 class Words &words , class Phrases &phrases ) ;

	// sets m_qterms[] array from the m_qwords[] array
	bool setQTerms ( class Words &words , class Phrases &phrases ) ;

	// . query expansion functions, the first gets all possible candidates
	//   (eliminated after Msg37 returns), the second actually modifies the
	//   query to include new terms
	//int32_t getCandidates(int32_t *synMap, int64_t *synIds, int32_t num);
	//void fixTermFreqs ( int32_t      *synMap          ,
	//		    int32_t       numTermsAndSyns ,
	//		    int64_t *termFreqs       ) ;
	//int32_t filterCandidates ( int32_t      *synMap          ,
	//			int64_t *synIds          ,
	//			int32_t       numTermsAndSyns ,
	//			int64_t *termFreqs       ,
	//			char      *coll            ) ;
	//bool expandQuery (int32_t *synMap, int64_t *synIds, int32_t num,
	//		  int64_t *termFreqs);

	// set m_expressions[] and m_operands[] arrays and m_numOperands 
	// for boolean queries
	bool setBooleanOperands ( );

	// helper funcs for parsing query into m_qwords[]
	//char        getFieldCode ( char *s , int32_t len , bool *hasColon ) ;
	bool        isConnection ( char *s , int32_t len ) ;

	// set the QueryTerm::m_hasNOT members
	//void setHasNOTs();

	// . used by IndexTable.cpp to make a ptr map of the query terms
	//   to make intersecting the termlists one at a time efficient
	// . "imap" is a list of the termlist numbers, but especially sorted
	// . 0 <= imap[i] < m_numTerms
	// . sizes[i] is the total docids for query term #i (up to the current
	//   tier being examined in IndexTable.cpp)
	// . we set blocksize[i] only when imap[i] is a termlist which is not
	//   a signless phrase. it is a number, N, such that
	//   imap[i], imap[i+1], ... imap[i+N-1] are a "block" that has all
	//   the signless phrase terms that contain query term # imap[i].
	//   we cluster them together like this because IndexTable needs to
	//   hash them all together since the phrase terms can imply the single
	//   terms.
	// . it now returns the number of terms put into imap[]
	// . it sets *retNumBlocks to the number of blocks put into 
	//   blocksizes[]
	// . "sizes" is the size of each list (all tiers combined). this is
	//   in query term num space, not IMAP space, and must be provided by
	//   the caller. it is the only arg that is input, the rest are output.
	int32_t getImap ( int32_t *sizes , int32_t *imap , int32_t *blocksizes ,
		       int32_t *retNumBlocks );


	// . replace sequences of UOR'd terms with a single termid
	// . the sequence of UOR'd terms are the component terms
	// . the term that replaces that sequence is the compound term
	// . the compound termlist will be a merge of the components' termlists
	// . this sets the component terms QueryTerm::m_ignore char to true
	//   when they are replaced by a compound term
	// . ensures compound term inherits the common QueryTerm::m_explicitBit
	//   from the component terms it replaced
	// . ensures the compound term's m_termFreqs[i] is the sum of the 
	//   components' termFreqs
	// . sets QueryTerm::m_component and QueryTerm::m_compound respectively
	//void addCompoundTerms ();

 public:

	// hash of all the query terms
	int64_t getQueryHash();

	bool isCompoundTerm ( int32_t i ) ;

	class QueryTerm *getQueryTermByTermId64 ( int64_t termId ) {
		for ( int32_t i = 0 ; i < m_numTerms ; i++ ) {
			if ( m_qterms[i].m_termId == termId ) 
				return &m_qterms[i];
		}
		return NULL;
	};

	// for debugging fhtqt mem leak
	char *m_st0Ptr;

	// silly little functions that support the BIG HACK
	//int32_t getNumNonFieldedSingletonTerms() { return m_numTermsSpecial; };
	//int32_t getTermsFound ( Query *q , char *foundTermVector ) ;

	// return -1 if does not exist in query, otherwise return the 
	// query word num
	int32_t getWordNum ( int64_t wordId );

	// this is now just used for boolean queries to deteremine if a docid
	// is a match or not
	unsigned char *m_bitScores ;
	int32_t           m_bitScoresSize;
	// . map explicit bits vector to implied bits vector
	// . like m_bitScores but simpler
	//qvec_t *m_bmap ;
	//int32_t    m_bmapSize;

	// one bmap per byte of qvec_t
	qvec_t m_bmap[sizeof(qvec_t)][256];

	// . bit vector that is 1-1 with m_qterms[]
	// . only has bits that we must have if we were default AND
	//uint16_t m_requiredBits;
	qvec_t         m_requiredBits;
	qvec_t         m_matchRequiredBits;
	qvec_t         m_negativeBits;
	qvec_t         m_forcedBits;
	// bit vector for terms that are synonyms
	qvec_t         m_synonymBits;  
	int32_t           m_numRequired;

	// language of the query
	uint8_t m_langId;

	bool m_useQueryStopWords;

	// use a generic buffer for m_qwords and m_expressions to point into
	// so we don't have to malloc for them
	char      m_gbuf [ GBUF_SIZE ];
	char     *m_gnext;

	QueryWord *m_qwords ; // [ MAX_QUERY_WORDS ];
	int32_t       m_numWords;
	int32_t       m_qwordsAllocSize;

	// QueryWords are converted to QueryTerms
	//QueryTerm m_qterms [ MAX_QUERY_TERMS ];
	int32_t      m_numTerms;
	int32_t      m_numTermsSpecial;

	int32_t m_numTermsUntruncated;

	// separate vectors for easier interfacing, 1-1 with m_qterms
	//int64_t m_termFreqs      [ MAX_QUERY_TERMS ];
	//int64_t m_termIds        [ MAX_QUERY_TERMS ];
	//char      m_termSigns      [ MAX_QUERY_TERMS ];
	//int32_t      m_componentCodes [ MAX_QUERY_TERMS ];
	//char      m_ignore         [ MAX_QUERY_TERMS ]; // is term ignored?
	SafeBuf    m_stackBuf;
	QueryTerm *m_qterms         ;
	//int64_t   *m_termIds        ;
	//char      *m_termSigns      ;
	//int32_t   *m_componentCodes ;
	//char      *m_ignore         ; // is term ignored?

	int32_t   m_numComponents;

	// how many bits in the full vector?
	//int32_t      m_numExplicitBits;

	// how many terms are we ignoring?
	//int32_t m_numIgnored;

	// site: field will disable site clustering
	// ip: field will disable ip clustering
	// site:, ip: and url: queries will disable caching
	bool m_hasPositiveSiteField;
	bool m_hasIpField;
	bool m_hasUrlField;
	bool m_hasSubUrlField;
	bool m_hasIlinkField;
	bool m_hasGBLangField;
	bool m_hasGBCountryField;
	char m_hasQuotaField;

	// query id set by Msg39.cpp
	int32_t m_qid;

	// . we set this to true if it is a boolean query
	// . when calling Query::set() above you can tell it explicitly
	//   if query is boolean or not, OR you can tell it to auto-detect
	//   by giving different values to the "boolFlag" parameter.
	bool m_isBoolean;

	int32_t m_synTerm;		// first term that's a synonym
	class SynonymInfo *m_synInfo;
	int32_t m_synInfoAllocSize;

	// if they got a gbdocid: in the query and it's not boolean, set these
	int64_t m_docIdRestriction;
	class Host *m_groupThatHasDocId;

	// for holding the filtered query, in utf8
	//char m_buf [ MAX_QUERY_LEN ];
	//int32_t m_bufLen;

	// for holding the filtered query, in utf8
	SafeBuf m_sb;
	char m_tmpBuf3[128];

	// for holding the filtered/NULL-terminated query for doing
	// matching. basically store phrases in here without punct
	// so we can point a needle to them for matching in XmlDoc.cpp.
	//char m_needleBuf [ MAX_QUERY_LEN + 1 ];
	//int32_t m_needleBufLen;

	// the original query
	//char m_orig [ MAX_QUERY_LEN ];
	//int32_t m_origLen;

	char *m_orig;
	int32_t m_origLen;
	SafeBuf m_osb;
	char m_otmpBuf[128];

	// we just have a ptr to this so don't pull the rug out
	//char *m_coll;
	//int32_t  m_collLen;
	
	// . we now contain the parsing components for boolean queries
	// . m_expressions points into m_gbuf or is allocated
	//class Expression *m_expressions; // [ MAX_OPERANDS ];
	//int32_t              m_expressionsAllocSize;
	Expression        m_expressions[MAX_EXPRESSIONS];
	int32_t              m_numExpressions;
	//class Operand     m_operands    [ MAX_OPERANDS ];
	//int32_t              m_numOperands ;

	// does query contain the pipe operator
	bool m_piped;

	int32_t m_maxQueryTerms ;

	bool m_queryExpansion;

	bool m_truncated;
	bool m_hasDupWords;

	bool m_hasUOR;
	bool m_hasLinksOperator;

	bool m_bmapIsSet ;

	bool m_hasSynonyms;

	SafeBuf m_debugBuf;

	void *m_containingParent;
};
/*
class QueryScores {
public:
	QueryScores(){};
	~QueryScores(){};
	bool set(Query *q);
	void reset();

	int32_t getNumTerms    ( ) { return m_numTerms; } ;
	int32_t getScore       ( int32_t i ) { return m_scores[i]; } ;
	void setScore        (int32_t i, int32_t score) {m_scores[i] = score; };
	int64_t getTermId ( int32_t i ) { return m_q->getTermId(i); } ; 
	//int64_t getWordId ( int32_t i ) { return m_wordIds[i]; } ; 
private:
	Query *m_q;
	int32_t m_numTerms;
        int64_t *m_freqs;
	int32_t m_termPtrs  [ MAX_QUERY_TERMS ];
	int32_t m_scores  [ MAX_QUERY_TERMS ];
	//int32_t m_wordIds  [ MAX_QUERY_TERMS ];
};	
*/
	
bool queryTest();

#endif
