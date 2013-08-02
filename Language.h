
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
	long      m_collLen;
	long long m_termId;
	long      m_minRecSize;
	Msg20     m_msg20s[MAX_FRAG_SIZE];
	long      m_numMsg20sOutstanding;
	long      m_numMsg20sLaunched;
	long      m_numMsg20sReceived;
};

class StateDict{
 public:
	char      *m_dictBuf;
	long       m_dictBufSize;
	char      *m_buf;
	long       m_bufSize;
	char     **m_wordsPtr;
	long long *m_termIds;
	long long *m_termFreqs;
	long       m_numTuples;
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
	long       m_fileNum;
	char       m_buf[1026];
	Msg3a      m_msg3a;
	Query      m_q;
	long long  m_numerator;
	long long  m_denominator;
	};*/

typedef struct Reco{
	char reco[MAX_PHRASE_LEN];
	long score;
}Reco;

class Language {

 public:

	Language();
	~Language();

	void reset();

	bool init( char *unifiedBuf, long unifiedBufSize, long lang, 
		   long hostsPerSplit, unsigned long myHash );

	void setLang( long lang ) { m_lang = lang; };
	
	//bool makeAffinities();

	//long getPhrasePopularity ( char *s, unsigned long long h,
	//		       bool checkTitleRecDict );

	bool checkDict(char *s, long slen, char encodeType);

	bool getRecommendation( char *origWord, long origWordLen,
				char *recommendation, long recommendationLen,
				bool *found, long *score, long *popularity, 
				bool  forceReco = false );

	//long narrowPhrase ( char *request, char *phrases, long *pops, 
	//		    long maxPhrases );

	//bool generateDicts ( long numWordsToDump , char *coll );

	//bool convertLatin1DictToUTF8 ( char *infile );

	// needed for makeDict
	//bool       gotTermFreqs( StateDict *st );
	//StateDict *m_stateDict;

	// hash table of the dictionary
	HashTableT <unsigned long long, long>m_dict;

 private:
	long spellcheckDict();

	// always accepts only ascii chars. makeClean() converts unicode into
	// ascii
	bool getPhonetic( char *origWord, long origWordLen,
			  char *target, long targetLen );

	bool loadRules();

	bool loadSpellerDict( char *spellerBuf, long spellerbufSize,
			      long hostsPerSplit, unsigned long myHash );

	//bool loadTitleRecDicts( );

	//bool loadNarrow( char *spellerBuf, long spellerBufSize, 
	//		 long hostsPerSplit, unsigned long myHash );

	bool loadDictHashTable( );

	//bool genTopPopFile ( char *infile );

	bool genDistributedPopFile ( char *infile, unsigned long myHash );
	
	//bool cleanDictFile ( );

	bool makeClean( char *inBuf, long inBufSize,
			char *outBuf, long outBufSize );//, bool isUTF16 );
	
	//bool makePhonet( char *infile);

	//bool makeDict();

	//bool makeQueryFiles ( );

	//bool makeWikiFiles ( );

	bool loadWikipediaWords();

	bool loadMispelledWords();
	
	bool hasMispelling(char *phrase, long phraseLen);

	long tryPhonet( char *phonetTmp, char *origPhonet,
			char *origClean, long tryForScore,
			Reco *recos, long numRecos, long *lowestScore );

	long editDistance( char *a, char *b, long level, // starting level
			   long limit ); // maximum level

	long weightedAverage(long soundslikeScore, long wordScore);

	long limitEditDistance( char *a, char *b, long limit );

	long limit1EditDistance( char *a, char *b );

	long limit2EditDistance( char *a, char *b );

	long checkRest( char *a, char *b, long w, char *amax, long min );

	long check2( char *a, char *b, long w, char *amax, long min );

	short editDistance( char *a0, char *b0 );

	short reduceScore ( char *a, char *b );

	//bool makeWordFiles ( long numWordsToDump , long numWordsPerPhrase ,
	//		     char *coll );

	//bool makePopFiles ( long numWordsToDump , long numWordsPerPhrase ,
	//			    char *coll);

	//bool makeScoreFiles ( long maxWordsPerFile );

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
	long  m_lang;

	// what charset does this language use
	unsigned char    m_charset;

	// buffer to store the phonetic rules
	char   *m_rulesBuf;
	long    m_rulesBufSize;
	char  **m_rulesPtr;
	long    m_rulesPtrSize;
	long    m_numRules;
	// points to the index of each rule that starts with a new character
	long    m_ruleStarts[MAX_CHARS];
	// the chars that are in a phonet
	bool    m_ruleChars[MAX_CHARS];

	// buffers to store the dictionaries
	char  *m_distributedBuf;
	long   m_distributedBufSize;
	char **m_tuplePtr;
	long   m_tuplePtrSize;
	long   m_numTuples;

	// total number of phonets
	long m_numPhonets;

	// narrow phrase
	char  *m_narrowBuf;
	long   m_narrowBufSize;
	long   m_numNarrowPtrs;
	char **m_frntPtrs;
	char **m_bckPtrs;
	long  *m_frntCharPtrs;//[NUM_CHARS][NUM_CHARS][NUM_CHARS];
	long  *m_bckCharPtrs;//[NUM_CHARS][NUM_CHARS][NUM_CHARS];

	// m_phonetics stores the hash of the phonetic as the key.
	// the value is a composite of index in m_tuplePtrs where the list
	// starts as the high 32 bits of the value and the number of 
	// words having the same phonetic as the low 32 bits of the value
	HashTableT <unsigned long long, unsigned long long > m_phonetics;

	// hash table of the distributed pop words dictionary
	//	HashTableT <unsigned long, long> m_titlerecDict;

	// hash table of the distributed pop words dictionary
	HashTableT <unsigned long long, long>m_distributedPopPhrases;

	// hash table of the top popular words in the dictionary
	//	HashTableT <unsigned long, char *> m_topPopPhrases;

	// hash table of mispelled words
	HashTableT <unsigned long, bool>m_misp;

	// hash table of wikipedia words
	HashTableT <unsigned long, bool>m_wiki;

	// PARMS, which can be adjusted. Currently all languages have the 
	// same adjustments, so using the same parms.
	long m_editDistanceWeightsDel1;
	long m_editDistanceWeightsDel2;
	long m_editDistanceWeightsSwap;
	long m_editDistanceWeightsSub;
	long m_editDistanceWeightsSimilar;
	long m_editDistanceWeightsMin;
	long m_editDistanceWeightsMax;
	long m_soundslikeWeight;
	long m_wordWeight;
	long m_span;

	bool m_followup;
	bool m_collapseResult;
	bool m_removeAccents;
};

#endif
