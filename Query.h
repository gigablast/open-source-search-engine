// Matt Wells, copyright Aug 2003

// Query is a class for parsing queries

#ifndef _QUERY_H_
#define _QUERY_H_

#include "SafeBuf.h"
#include "Mem.h"

// keep these down to save memory
//#define MAX_QUERY_LEN   8000 // url:XXX can be quite long! (MAX_URL_LEN)
#define MAX_QUERY_LEN 3200
// . words need to deal with long list of sites!
// . remember, words can be string of punctuation, too
//#define MAX_QUERY_WORDS 5000 
//#define MAX_QUERY_WORDS 32000 
// not any more!
#define MAX_QUERY_WORDS 320 

// . how many IndexLists might we get/intersect
// . we now use a long long to hold the query term bits for non-boolean queries
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
#define MAX_QUERY_TERMS 160

// only allow up to 200 interests from facebook plus manually entered
// because we are limited by the query terms above so we can only
// UOR so many in SearchInput.cpp
#define MAX_INTERESTS 200

#define GBUF_SIZE (16*1024)
#define SYNBUF_SIZE (16*1024)

// score of highest-scoring query term in the QueryScore
//#define BASE_QUERY_SCORE 10000000

// let's support up to 64 query terms for now
typedef unsigned long long qvec_t;

#define MAX_EXPLICIT_BITS (sizeof(qvec_t)*8)

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

#define FIELD_GBOTHER 92

// returns a FIELD_* code above, or FIELD_GENERIC if not in the list
char getFieldCode  ( char *s , long len , bool *hasColon = NULL ) ;
char getFieldCode2 ( char *s , long len , bool *hasColon = NULL ) ;
char getFieldCode3 ( long long h64 ) ;

long getNumFieldCodes ( );

struct QueryField {
	char *text;
	char field;
	bool hasColon;
	char *desc;
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
	long set ( long a , long b , class QueryWord *qwords , long level ,
		   bool underNOT ) ;
	// . "bits" are 1-1 with the query terms in Query::m_qterms[] array
	// . Operand::m_termBits is the required bits for operand to be true
	// . does not include signless phrases
	bool isTruth ( qvec_t bits, qvec_t mask=(qvec_t)-1 ) {
		// must always satisfy hard required terms (+ sign)
		//if ( (bits & m_forcedBits) != m_forcedBits )
		//	return false;
		if (m_hasNOT) return (bits & m_termBits & mask) == 0;
		return ( (bits & m_termBits & mask) == (m_termBits & mask)); 
		// . we are now back to good ol' default OR
		// . m_termBits should have been masked with
		//   m_requiredBits so as not to include signless phrases
		//return ( (bits & m_termBits) != 0 ); 
	};
	void print (SafeBuf *sbuf);
	// we are a sequence of QueryWords
	//long m_startWordNum;
	//long m_lastWordNum;
	// . we treat the required term bits of those words as one unit (ANDed)
	// . unsigned phrases are not included in these term bits
	// . doc just needs one of these bits for this op to be considered true
	qvec_t m_termBits;
	bool   m_hasNOT;
	class Expression *m_parent;

	// we MUST have these for this OPERAND to be true
	//unsigned short m_forcedBits;
};

// operand1 AND operand2 OR  ...
// operand1 OR  operand2 AND ...
class Expression {
public:
	long set (long start, 
		   long end, 
		   long pos, // current parsing position
		   class Query      *q,
		   long              level, 
		   class Expression *parent, 
		   class Expression *leftChild,
		   bool hasNOT ,
		  bool underNOT );

	bool isTruth ( qvec_t bits, qvec_t mask=(qvec_t)-1  ) ;
	// . what QueryTerms are UNDER the influence of the NOT opcode?
	// . we read in the WHOLE termlist of those that are (like '-' sign)
	// . returned bit vector is 1-1 with m_qterms in Query class
	qvec_t getNOTBits ( bool hasNOT );
	void print (SafeBuf *sbuf);
	// . a list of operands separated by op codes (a AND b OR c ...)
	// . sometimes and operand is another expression: a AND (b OR c)
	// . use NULL in m_operands slot if we got an expression and vice versa
	// . m_opcodes[i] is the opcode after operand #i
	class Expression *m_parent;
	//class Operand    *m_operands    [ MAX_OPERANDS ];
	class Expression *m_children [ MAX_OPERANDS ];
	//char              m_opcodes     [ MAX_OPERANDS ];
	//long              m_numOperands;
	// now expressions can have either child expressions or 1 operand
	long              m_numChildren;
	// do we have a NOT operator before operand #i?
	//bool              m_hasNOT      [ MAX_OPERANDS ];
	// only one opcode, operand, hasNOT per expression now
	uint8_t           m_opcode;
	class Operand    *m_operand;
	bool              m_hasNOT;
	// needed for nesting
	long              m_start;
	long              m_end;

};

////////////////////////////////////////////////////////
////////////////////////////////////////////////////////
//////////         END BOOLEAN STUFF       /////////////
////////////////////////////////////////////////////////
////////////////////////////////////////////////////////




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
	//UCScript wordScript() { 
	//	UChar*foo;
	//	return ucGetScript(utf16Decode((UChar*)(m_word),&foo));
	//}

	// this ptr references into the actual query
	char       *m_word    ;
	long        m_wordLen ;
	// the length of the phrase, if any. it starts at m_word
	long        m_phraseLen;
	// this is the term hash with collection and field name and
	// can be looked up directly in indexdb
	long long   m_wordId ;
	long long   m_phraseId;
	// hash of field name then collection, used to hash termId
	long long   m_prefixHash;

	// are we in a phrase in a wikipedia title?
	long        m_wikiPhraseId;
	long        m_wikiPhraseStart;
	long        m_numWordsInWikiPhrase;

	// . this is just the hash of m_term and is used for highlighting, etc.
	// . it is 0 for terms in a field?
	long long   m_rawWordId ;
	long long   m_rawPhraseId ;
	// if we are phrase, the end word's raw id
	long long   m_rightRawWordId;
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
	// is this query single word in quotes?
	bool        m_inQuotes ; 
	// is this word in a phrase that is quoted?
	bool        m_inQuotedPhrase;
	// what word # does the quote we are in start at?
	long        m_quoteStart;
	long        m_quoteEnd; // inclusive!
	// are we connected to the alnum word on our left/right?
	bool        m_leftConnected;
	bool        m_rightConnected;
	// if we're in middle or right end of a phrase, where does it start?
	long        m_leftPhraseStart;
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
	long m_userWeight;
	char m_userType;
	long m_userWeightPhrase;
	char m_userTypePhrase;
	bool m_queryOp;
	// is it after a NOT operator? i.e. NOT ( x UOR y UOR ... )
	bool m_underNOT;
	// is this query word before a | (pipe) operator?
	bool m_piped;
	// used by Matches.cpp for highlighting under different colors
	long m_colorNum;
};

// . we filter the QueryWords and turn them into QueryTerms
// . QueryTerms are the important parts of the QueryWords
class QueryTerm {

 public:
	// the query word we were derived from
	QueryWord *m_qword;
	// . are we a phrase termid or single word termid from that QueryWord?
	// . the QueryWord instance represents both, so we must choose
	bool       m_isPhrase;
	// for compound phrases like, "cat dog fish" we do not want docs
	// with "cat dog" and "dog fish" to match, so we extended our hackfix
	// in Summary.cpp to use m_phrasePart to do this post-query filtering
	long       m_phrasePart;
	// this is phraseId for phrases, and wordId for words
	long long  m_termId;
	// used by Matches.cpp
	long long  m_rawTermId;

	// . if we are a phrase these are the termids of the word that
	//   starts the phrase and the word that ends the phrase respectively
	long long  m_rightRawWordId;
	long long  m_leftRawWordId;

	// sign of the phrase or word we used
	char       m_termSign;
	// our representative bit (up to 16 MAX_QUERY_TERMS)
	//unsigned short m_explicitBit;
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
	// point to term, either m_word or m_phrase
	char      *m_term;
	long       m_termLen;
	// point to the posdblist that represents us
	class RdbList   *m_posdbListPtr;
	// . our representative bits
	// . the bits in this bit vector is 1-1 with the QueryTerms
	// . if a doc has query term #i then bit #i will be set
	// . if a doc EXplicitly has phrase "A B" then it may have 
	//   term A and term B implicity
	// . therefore we also OR the bits for term A and B into m_implicitBits
	// . THIS SHIT SHOULD be just used in setBitScores() !!!
	//unsigned short m_implicitBits;
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
	long m_userWeight;
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
	//long m_affinity; 	// affinity to the synonym
	QueryTerm *m_synonymOf;
	long long m_synWids0;
	long long m_synWids1;
	long      m_numAlnumWordsInSynonym;
	// like if we are the "nj" syn of "new jersey", this will be 2 words
	// since "new jersey", our base, is 2 alnum words.
	long      m_numAlnumWordsInBase;
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
	long  m_leftPhraseTermNum;
	long  m_rightPhraseTermNum;
	// same as above basically
	class QueryTerm *m_leftPhraseTerm;
	class QueryTerm *m_rightPhraseTerm;
	// for scoring summary sentences from XmlDoc::getEventSummary()
	float m_score;

	char m_startKey[MAX_KEY_BYTES];
	char m_endKey  [MAX_KEY_BYTES];
	char m_ks;

	// used by Msg40.cpp for gigabits generation
	long long m_hash64d;
	long      m_popWeight;
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
		    //long  queryLen , 
		    //char *coll     , 
		    //long  collLen  ,
		    uint8_t  langId ,
		    char     queryExpansion ,
		    bool     useQueryStopWords = true );
		   //char  boolFlag = 2 , // auto-detect if boolean query
		   //bool  keepAllSingles = false ,
		   //long  maxQueryTerms = 0x7fffffff );

	// serialize/deserialize ourselves so we don't have to pass the
	// unmodified string around and reparse it every time
	long    getStoredSize();
	long 	serialize(char *buf, long bufLen);
	long	deserialize(char *buf, long bufLen);

	// . if a term is truncated in indexdb, change its '+' sign to a '*'
	// . will recopmute m_bitScores to fix bit #7
	//void softenTruncatedTerms ( );

	bool setQueryTermScores ( long long *termFreqsArg ) ;

	// about how hits for this query?
	//long long getEstimatedTotalHits ( );

	char *getQuery    ( ) { return m_orig  ; };
	long  getQueryLen ( ) { return m_origLen; };

	//long  getNumIgnored    ( ) { return m_numIgnored; };
	//long  getNumNotIgnored ( ) { return m_numTerms ;  };

	long       getNumTerms  (        ) { return m_numTerms;              };
	char       getTermSign  ( long i ) { return m_qterms[i].m_termSign;  };
	bool       isPhrase     ( long i ) { return m_qterms[i].m_isPhrase;  };
	bool       isInPhrase   ( long i ) { return m_qterms[i].m_inPhrase;  };
	bool       isInQuotes   ( long i ) { return m_qterms[i].m_inQuotes;  };
	long long  getTermId    ( long i ) { return m_qterms[i].m_termId;    };
	char       getFieldCode2( long i ) { return m_qterms[i].m_fieldCode; };
	long long  getRawTermId ( long i ) { return m_qterms[i].m_rawTermId; };
	char      *getTerm      ( long i ) { return m_qterms[i].m_term; };
	long       getTermLen   ( long i ) { return m_qterms[i].m_termLen; };
	bool       isQueryStopWord (long i ) { 
		return m_qterms[i].m_isQueryStopWord; };
	// . not HARD required, but is term #i used for an EXACT match?
	// . this includes negatives and phrases with signs in addition to
	//   the standard signless single word query term
	bool       isRequired ( long i ) { 
		if ( ! m_qterms[i].m_isPhrase ) return true;
		if (   m_qterms[i].m_termSign ) return true;
		return false;
	};

	//long getNumRequired ( ) ;
	bool isSplit();

	bool isSplit(long i) { return m_qterms[i].isSplit(); };

	// . Msg39 calls this to get our vector so it can pass it to Msg37
	// . the signs and ids are dupped in the QueryTerm classes, too
	//long long *getTermFreqs ( ) { return m_termFreqs ; };
	//long long  getTermFreq  ( long i ) { return m_termFreqs[i]; };
	long long *getTermIds   ( ) { return m_termIds   ; };
	char      *getTermSigns ( ) { return m_termSigns ; };
	long      *getComponentCodes   ( ) { return m_componentCodes; };
	long long  getRawWordId ( long i ) { return m_qwords[i].m_rawWordId;};

	long getNumComponentTerms ( ) { return m_numComponents; };

	// sets m_bmap[][] so getImplicits() works
	void setBitMap ( );
	bool testBoolean(qvec_t bits, qvec_t bitmask=(qvec_t)-1);
	// print to log
	void printBooleanTree();
	void printQueryTerms();



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

	//qvec_t getImplicitBitsFromTermNum ( long qtnum ) {
	//};

	// returns false if no truths possible
	bool setBitScoresBoolean ( char *buf , long bufSize );

	// ALWAYS call this after calling setBitScores(), it uses m_bitScores[]
	//long long getEstimatedTotalHitsBoolean ( );

	// sets m_qwords[] array, this function is the heart of the class
	bool setQWords ( char boolFlag , bool keepAllSingles ,
			 class Words &words , class Phrases &phrases ) ;

	// sets m_qterms[] array from the m_qwords[] array
	bool setQTerms ( class Words &words , class Phrases &phrases ) ;

	// . query expansion functions, the first gets all possible candidates
	//   (eliminated after Msg37 returns), the second actually modifies the
	//   query to include new terms
	//long getCandidates(long *synMap, long long *synIds, long num);
	//void fixTermFreqs ( long      *synMap          ,
	//		    long       numTermsAndSyns ,
	//		    long long *termFreqs       ) ;
	//long filterCandidates ( long      *synMap          ,
	//			long long *synIds          ,
	//			long       numTermsAndSyns ,
	//			long long *termFreqs       ,
	//			char      *coll            ) ;
	//bool expandQuery (long *synMap, long long *synIds, long num,
	//		  long long *termFreqs);

	// set m_expressions[] and m_operands[] arrays and m_numOperands 
	// for boolean queries
	bool setBooleanOperands ( );

	// helper funcs for parsing query into m_qwords[]
	//char        getFieldCode ( char *s , long len , bool *hasColon ) ;
	bool        isConnection ( char *s , long len ) ;

	// set the QueryTerm::m_hasNOT members
	void setHasNOTs();

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
	long getImap ( long *sizes , long *imap , long *blocksizes ,
		       long *retNumBlocks );


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
	long long getQueryHash();

	bool isCompoundTerm ( long i ) ;

	// silly little functions that support the BIG HACK
	//long getNumNonFieldedSingletonTerms() { return m_numTermsSpecial; };
	//long getTermsFound ( Query *q , char *foundTermVector ) ;

	// return -1 if does not exist in query, otherwise return the 
	// query word num
	long getWordNum ( long long wordId );

	// this is now just used for boolean queries to deteremine if a docid
	// is a match or not
	unsigned char *m_bitScores ;
	long           m_bitScoresSize;
	// . map explicit bits vector to implied bits vector
	// . like m_bitScores but simpler
	//qvec_t *m_bmap ;
	//long    m_bmapSize;

	// one bmap per byte of qvec_t
	qvec_t m_bmap[sizeof(qvec_t)][256];

	// . bit vector that is 1-1 with m_qterms[]
	// . only has bits that we must have if we were default AND
	//unsigned short m_requiredBits;
	qvec_t         m_requiredBits;
	qvec_t         m_matchRequiredBits;
	qvec_t         m_negativeBits;
	qvec_t         m_forcedBits;
	// bit vector for terms that are synonyms
	qvec_t         m_synonymBits;  
	long           m_numRequired;

	// language of the query
	uint8_t m_langId;

	bool m_useQueryStopWords;

	// use a generic buffer for m_qwords and m_expressions to point into
	// so we don't have to malloc for them
	char      m_gbuf [ GBUF_SIZE ];
	char     *m_gnext;

	QueryWord *m_qwords ; // [ MAX_QUERY_WORDS ];
	long       m_numWords;
	long       m_qwordsAllocSize;

	// QueryWords are converted to QueryTerms
	QueryTerm m_qterms [ MAX_QUERY_TERMS ];
	long      m_numTerms;
	long      m_numTermsSpecial;

	// separate vectors for easier interfacing, 1-1 with m_qterms
	//long long m_termFreqs      [ MAX_QUERY_TERMS ];
	long long m_termIds        [ MAX_QUERY_TERMS ];
	char      m_termSigns      [ MAX_QUERY_TERMS ];
	long      m_componentCodes [ MAX_QUERY_TERMS ];
	char      m_ignore         [ MAX_QUERY_TERMS ]; // is term ignored?
	long      m_numComponents;

	// how many bits in the full vector?
	//long      m_numExplicitBits;

	// how many terms are we ignoring?
	//long m_numIgnored;

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
	long m_qid;

	// . we set this to true if it is a boolean query
	// . when calling Query::set() above you can tell it explicitly
	//   if query is boolean or not, OR you can tell it to auto-detect
	//   by giving different values to the "boolFlag" parameter.
	bool m_isBoolean;

	long m_synTerm;		// first term that's a synonym
	class SynonymInfo *m_synInfo;
	long m_synInfoAllocSize;

	// if they got a gbdocid: in the query and it's not boolean, set these
	long long m_docIdRestriction;
	class Host *m_groupThatHasDocId;

	// for holding the filtered query, in utf8
	char m_buf [ MAX_QUERY_LEN ];
	long m_bufLen;

	// for holding the filtered/NULL-terminated query for doing
	// matching. basically store phrases in here without punct
	// so we can point a needle to them for matching in XmlDoc.cpp.
	char m_needleBuf [ MAX_QUERY_LEN + 1 ];
	long m_needleBufLen;

	// the original query
	char m_orig [ MAX_QUERY_LEN ];
	long m_origLen;

	// we just have a ptr to this so don't pull the rug out
	//char *m_coll;
	//long  m_collLen;
	
	// . we now contain the parsing components for boolean queries
	// . m_expressions points into m_gbuf or is allocated
	class Expression *m_expressions; // [ MAX_OPERANDS ];
	long              m_expressionsAllocSize;
	long              m_numExpressions;
	class Operand     m_operands    [ MAX_OPERANDS ];
	long              m_numOperands ;

	// does query contain the pipe operator
	bool m_piped;

	long m_maxQueryTerms ;

	bool m_queryExpansion;

	bool m_truncated;
	bool m_hasDupWords;

	bool m_hasUOR;
	bool m_hasLinksOperator;

	bool m_bmapIsSet ;

	bool m_hasSynonyms;

	SafeBuf m_debugBuf;
};
/*
class QueryScores {
public:
	QueryScores(){};
	~QueryScores(){};
	bool set(Query *q);
	void reset();

	long getNumTerms    ( ) { return m_numTerms; } ;
	long getScore       ( long i ) { return m_scores[i]; } ;
	void setScore        (long i, long score) {m_scores[i] = score; };
	long long getTermId ( long i ) { return m_q->getTermId(i); } ; 
	//long long getWordId ( long i ) { return m_wordIds[i]; } ; 
private:
	Query *m_q;
	long m_numTerms;
        long long *m_freqs;
	long m_termPtrs  [ MAX_QUERY_TERMS ];
	long m_scores  [ MAX_QUERY_TERMS ];
	//long m_wordIds  [ MAX_QUERY_TERMS ];
};	
*/
	
bool queryTest();

#endif
