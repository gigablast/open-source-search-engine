
#ifndef _LANGUAGE_H_
#define _LANGUAGE_H_
//#include <wchar.h>
#include "gb-include.h"
//#include "UnicodeProperties.h" //UChar32
#include "File.h"
#include "HashTableT.h"
#include "Query.h"
#include "Lang.h"
#include "Multicast.h"
#include "Threads.h"
#include "Titledb.h"
#include "Iso8859.h"
#include "IndexList.h"
//#include "Msg3a.h"

#include "Msg20.h"
#include "Msg37.h"

// max chars in any language
#define MAX_WORDS_PER_PHRASE 5
#define MAX_CHARS 256
#define TOP_POP_PHRASES 40 * 1024
#define NUM_CHARS 40
#define MAX_FRAG_SIZE 1024
// max chars that start the rule

#define MAX_PHRASE_LEN 80
#define MAX_RECOMMENDATIONS 10
#define LARGE_SCORE 0xfffff
#define MAX_NARROW_SEARCHES 19

/*
// used only while generating titles from wikipedia pages, makeWikiFiles()
class StateWik {
public:
	bool getIndexList(  );
	bool getSummary (  );
	bool gotSummary (  );

	int       m_fdw;
	Msg0      m_msg0;
	IndexList m_list;
	Query     m_q;
	key_t     m_startKey;
	key_t     m_endKey;
	char     *m_coll;
	int32_t      m_collLen;
	int64_t m_termId;
	int32_t      m_minRecSize;
	Msg20     m_msg20s[MAX_FRAG_SIZE];
	int32_t      m_numMsg20sOutstanding;
	int32_t      m_numMsg20sLaunched;
	int32_t      m_numMsg20sReceived;
};

class StateDict{
 public:
	char      *m_dictBuf;
	int32_t       m_dictBufSize;
	char      *m_buf;
	int32_t       m_bufSize;
	char     **m_wordsPtr;
	int64_t *m_termIds;
	int64_t *m_termFreqs;
	int32_t       m_numTuples;
	Msg37      m_msg37;
};
*/

/*class StateAff{
 public:
	bool openAffinityFile ( );
	bool launchAffinity ( );
	bool gotAffinityFreqs1 ( );
	bool gotAffinityFreqs2 ( );
	bool doneAffinities ( );

	FILE      *m_fdr;
	int        m_fdw;
	int32_t       m_fileNum;
	char       m_buf[1026];
	Msg3a      m_msg3a;
	Query      m_q;
	int64_t  m_numerator;
	int64_t  m_denominator;
	};*/

typedef struct Reco{
	char reco[MAX_PHRASE_LEN];
	int32_t score;
}Reco;

class Language {

 public:

	Language();
	~Language();

	void reset();

	bool init( char *unifiedBuf, int32_t unifiedBufSize, int32_t lang, 
		   int32_t hostsPerSplit, uint32_t myHash );

	void setLang( int32_t lang ) { m_lang = lang; };
	
	//bool makeAffinities();

	//int32_t getPhrasePopularity ( char *s, uint64_t h,
	//		       bool checkTitleRecDict );

	bool checkDict(char *s, int32_t slen, char encodeType);

	bool getRecommendation( char *origWord, int32_t origWordLen,
				char *recommendation, int32_t recommendationLen,
				bool *found, int32_t *score, int32_t *popularity, 
				bool  forceReco = false );

	//int32_t narrowPhrase ( char *request, char *phrases, int32_t *pops, 
	//		    int32_t maxPhrases );

	//bool generateDicts ( int32_t numWordsToDump , char *coll );

	//bool convertLatin1DictToUTF8 ( char *infile );

	// needed for makeDict
	//bool       gotTermFreqs( StateDict *st );
	//StateDict *m_stateDict;

	// hash table of the dictionary
	HashTableT <uint64_t, int32_t>m_dict;

 private:
	int32_t spellcheckDict();

	// always accepts only ascii chars. makeClean() converts unicode into
	// ascii
	bool getPhonetic( char *origWord, int32_t origWordLen,
			  char *target, int32_t targetLen );

	bool loadRules();

	bool loadSpellerDict( char *spellerBuf, int32_t spellerbufSize,
			      int32_t hostsPerSplit, uint32_t myHash );

	//bool loadTitleRecDicts( );

	//bool loadNarrow( char *spellerBuf, int32_t spellerBufSize, 
	//		 int32_t hostsPerSplit, uint32_t myHash );

	bool loadDictHashTable( );

	//bool genTopPopFile ( char *infile );

	bool genDistributedPopFile ( char *infile, uint32_t myHash );
	
	//bool cleanDictFile ( );

	bool makeClean( char *inBuf, int32_t inBufSize,
			char *outBuf, int32_t outBufSize );//, bool isUTF16 );
	
	//bool makePhonet( char *infile);

	//bool makeDict();

	//bool makeQueryFiles ( );

	//bool makeWikiFiles ( );

	bool loadWikipediaWords();

	bool loadMispelledWords();
	
	bool hasMispelling(char *phrase, int32_t phraseLen);

	int32_t tryPhonet( char *phonetTmp, char *origPhonet,
			char *origClean, int32_t tryForScore,
			Reco *recos, int32_t numRecos, int32_t *lowestScore );

	int32_t editDistance( char *a, char *b, int32_t level, // starting level
			   int32_t limit ); // maximum level

	int32_t weightedAverage(int32_t soundslikeScore, int32_t wordScore);

	int32_t limitEditDistance( char *a, char *b, int32_t limit );

	int32_t limit1EditDistance( char *a, char *b );

	int32_t limit2EditDistance( char *a, char *b );

	int32_t checkRest( char *a, char *b, int32_t w, char *amax, int32_t min );

	int32_t check2( char *a, char *b, int32_t w, char *amax, int32_t min );

	int16_t editDistance( char *a0, char *b0 );

	int16_t reduceScore ( char *a, char *b );

	//bool makeWordFiles ( int32_t numWordsToDump , int32_t numWordsPerPhrase ,
	//		     char *coll );

	//bool makePopFiles ( int32_t numWordsToDump , int32_t numWordsPerPhrase ,
	//			    char *coll);

	//bool makeScoreFiles ( int32_t maxWordsPerFile );

	// this map maps a char to a "dict char"
	//unsigned char m_map [ 256 ];

	// . when comparing letter pairs, we only allow them to consist of
	//   certain chars: 0-9, A-Z, apostrophe and space and \0 otherwise
	//   m_table gets too big. This implies a NUM_CHARS of 
	// . this compressed the value, too
	// . \0, space, 0-9, A-Z, \'   is the ordering
	//unsigned char to_dict_char ( unsigned char c ) { return m_map[c]; };

	// Temporary unicode workaround for latin-1 compatibility
	//unsigned char uc_to_dict_char ( UChar c ) { 
	//	if (c>255)c=0;
	//	return m_map[c]; 
	//};

	// what language loaded
	int32_t  m_lang;

	// what charset does this language use
	unsigned char    m_charset;

	// buffer to store the phonetic rules
	char   *m_rulesBuf;
	int32_t    m_rulesBufSize;
	char  **m_rulesPtr;
	int32_t    m_rulesPtrSize;
	int32_t    m_numRules;
	// points to the index of each rule that starts with a new character
	int32_t    m_ruleStarts[MAX_CHARS];
	// the chars that are in a phonet
	bool    m_ruleChars[MAX_CHARS];

	// buffers to store the dictionaries
	char  *m_distributedBuf;
	int32_t   m_distributedBufSize;
	char **m_tuplePtr;
	int32_t   m_tuplePtrSize;
	int32_t   m_numTuples;

	// total number of phonets
	int32_t m_numPhonets;

	// narrow phrase
	char  *m_narrowBuf;
	int32_t   m_narrowBufSize;
	int32_t   m_numNarrowPtrs;
	char **m_frntPtrs;
	char **m_bckPtrs;
	int32_t  *m_frntCharPtrs;//[NUM_CHARS][NUM_CHARS][NUM_CHARS];
	int32_t  *m_bckCharPtrs;//[NUM_CHARS][NUM_CHARS][NUM_CHARS];

	// m_phonetics stores the hash of the phonetic as the key.
	// the value is a composite of index in m_tuplePtrs where the list
	// starts as the high 32 bits of the value and the number of 
	// words having the same phonetic as the low 32 bits of the value
	HashTableT <uint64_t, uint64_t > m_phonetics;

	// hash table of the distributed pop words dictionary
	//	HashTableT <uint32_t, int32_t> m_titlerecDict;

	// hash table of the distributed pop words dictionary
	HashTableT <uint64_t, int32_t>m_distributedPopPhrases;

	// hash table of the top popular words in the dictionary
	//	HashTableT <uint32_t, char *> m_topPopPhrases;

	// hash table of mispelled words
	HashTableT <uint32_t, bool>m_misp;

	// hash table of wikipedia words
	HashTableT <uint32_t, bool>m_wiki;

	// PARMS, which can be adjusted. Currently all languages have the 
	// same adjustments, so using the same parms.
	int32_t m_editDistanceWeightsDel1;
	int32_t m_editDistanceWeightsDel2;
	int32_t m_editDistanceWeightsSwap;
	int32_t m_editDistanceWeightsSub;
	int32_t m_editDistanceWeightsSimilar;
	int32_t m_editDistanceWeightsMin;
	int32_t m_editDistanceWeightsMax;
	int32_t m_soundslikeWeight;
	int32_t m_wordWeight;
	int32_t m_span;

	bool m_followup;
	bool m_collapseResult;
	bool m_removeAccents;
};

#endif
