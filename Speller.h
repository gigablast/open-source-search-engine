// Matt Wells, copyright Sep 2003

// Speller is a class for doing spell checking on user queries.

// . TODO: we might be able to use this as a related searches feature too, but
//         we might have to use a different distance metric (getSimilarity()) 
//         that is more word based and less letter based.

#ifndef _SPELLER_H_
#define _SPELLER_H_

// max long returned by getPhrasePopularity() function
#define MAX_PHRASE_POP 16800

#include "StopWords.h"
#include "Language.h"
// . the height and width of m_stable[][] that takes a letter pair as an index
// . valid chars are returned by isValidChar() routine
// . we use A-Z, 0-9, space, hyphen, apostrophe and \0... that's it
// need this to distribute pop words dictionary.
#define MAX_UNIQUE_HOSTS_PER_SPLIT 16

class StateFrag{
 public:
	// ALL THESE ARE CONCERNED WITH THE FRAG
	void *m_state;//StateSpeller
	long  m_errno;
	Query *m_q;
	long  m_startQword;
	long  m_endQword;
	bool  m_recommended;
	// break the frag into word ptrs, it must be NULL terminated
	char *m_wp     [MAX_FRAG_SIZE];//[ MAX_QUERY_WORDS ];
	long  m_wplen  [MAX_FRAG_SIZE];//[ MAX_QUERY_WORDS ];
	bool  m_isstop [MAX_FRAG_SIZE];//[ MAX_QUERY_WORDS ];
	bool  m_isfound[MAX_FRAG_SIZE];//[ MAX_QUERY_WORDS ];
	// total number of words that have had recommendations
	long  m_numFound;
	char  m_dst[MAX_FRAG_SIZE];
	Multicast m_mcast[MAX_UNIQUE_HOSTS_PER_SPLIT];
	long  m_numRequests;
	long  m_numReplies;
	long  m_pLen;
	long  m_pPosn;
	char *m_a;
	long  m_alen;
	char *m_b;
	char  m_c;
	bool  m_narrowPhrase;
	long  m_numNarrowPhrases;
	char  m_narrowPhrases[MAX_NARROW_SEARCHES][MAX_FRAG_SIZE];
};


class StateSpeller{
 public:
	void *m_state;
	void (*m_callback)(void *state);
	Query *m_q;
	bool   m_spellcheck;
	char  *m_dst;
	char  *m_dend;
	bool   m_narrowSearch;
	char  *m_nrw;
	char  *m_nend;
	long  *m_numNarrow;
	unsigned long long m_start;
	long   m_numFrags;
	long   m_numFragsReceived;
	StateFrag *m_stFrag[MAX_FRAG_SIZE];
};

class Speller {

 public:

	Speller();
	~Speller();

	bool registerHandler();

	void reset();

	bool init();

	void test (char *ff);

	//uint8_t getUniqueLang ( long long *wid );
	long long getLangBits64 ( long long *wid ) ;

	long getPhrasePopularity ( char *s, unsigned long long h,
				   bool checkTitleRecDict,
				   unsigned char langId = langEnglish );

	bool canSplitWords ( char *s, long slen, bool *isPorn, 
			     char *splitWords,
			     unsigned char langId, long encodeType);
	
	bool findNext( char *s, char *send, char **nextWord, bool *isPorn,
			unsigned char langId, long encodeType );

	long checkDict ( char *s, long slen, char encodeType, 
			 unsigned char lang = langEnglish ){
		return m_language[lang].checkDict(s,slen,encodeType);
	}

	// should be same hash algo to make wordId
	bool isInDict ( uint64_t wordId ) {
		return m_unifiedDict.isInTable(&wordId); };
	
	// . dump out the first "numWordsToDump" words and phrases
	//   encountered will scanning the records in Titledb
	// . use these words/phrases to make the dictionaries
	bool generateDicts ( long numWordsToDump , char *coll );

	bool getPhonetic( char *word, char *target );

	bool getRecommendation ( Query *q, bool spellcheck, 
				 char *dst, long dstLen, 
				 bool narrowSearch,
				 char *narrow, long narrowLen, 
				 long *numNarrows, void *state,
				 void (*callback)(void *state));

	bool getRecommendation ( StateFrag *st );

	bool launchReco( StateFrag *st );

	bool gotSpellerReply( StateFrag *st );

	void gotFrags( void *state );

	bool getRecommendation ( char *frag , char *dst , long  dstLen );

	long getWords ( const char *s ,
			char *wp     [MAX_FRAG_SIZE] ,
			long  wplen  [MAX_FRAG_SIZE] ,
			bool *isstop                   );

	Language m_language[MAX_LANGUAGES];

	char *getRandomWord() ;
	bool loadUnifiedDict();
	bool createUnifiedDict ();

	void dictLookupTest ( char *ff );

	char *getPhraseRecord(char *phrase, int len);
	long long *getPhraseLanguages(char *phrase, int len);
	bool getPhraseLanguages(char *phrase, int len, long long *array);
	bool getPhraseLanguages2 (char *phraseRec , long long *array) ;
	char getPhraseLanguage(char *phrase, int len );
	bool getSynsInEnglish ( char *w , 
				long wlen ,
				char nativeLang ,
				char wikiLang ) ;
	void CheckWordRecs(const char *filename);
	//private:

	bool populateHashTable( char *ff, HashTableX *htable, 
				unsigned char langId );
	//private:
	//HashTableT <unsigned long long, char* > m_unifiedDict;
	HashTableX m_unifiedDict;

	// can this queryword start a phrase ?
	bool canStart( QueryWord *qw );

	//char *m_unifiedBuf;
	//long  m_unifiedBufSize;
	SafeBuf m_unifiedBuf;
	long  m_hostsPerSplit;
};

extern class Speller g_speller;

#endif
