// . class for linking words together for query expansion
// . all character strings should be treated as utf8

#ifndef _THESAURUS_H_
#define _THESAURUS_H_

#include "HashTableT.h"

#define MAX_AFFINITY LONG_MAX
#define MAX_SYNS     32	      // maximum number of synonym links
#define DEF_AFFINITY (long)(MAX_AFFINITY * 0.9)

struct Suffix {
	char *m_suffix;
	long m_len;
	long m_numReps;
	char **m_reps;
	long *m_repLens;
};

class StateAffinity {
public:
	uint64_t m_time;
	HashTableT<uint64_t, uint64_t>  m_hitsTable;
	HashTableT<uint64_t,  int64_t>  m_synTable;
	HashTableT<uint64_t,  int64_t> *m_newTable;
	HashTableT<uint64_t,  int64_t> *m_oldTable;
	class Thesaurus *m_thes;
	char *m_synstart;
	char *m_syn;
	char *m_synend;
	char *m_synonymText;
	char m_server[MAX_URL_LEN];
	long m_n;
	long m_next;
	long m_ip;
	long m_built;
	long m_skip;
	long m_sent;
	long m_recv;
	long m_errors;
	long m_cache;
	long m_old;
	bool m_fullRebuild;
	void (*m_callback)(void *);
};

// for m_type field, ranges 0-15 for the current data structure
#define SYN_SYNONYM 	0		// thesaurus
#define SYN_STEM    	1		// stemmer
#define SYN_SPELLING    2		// speller
#define SYN_ACRONYM     3		// thesaurus 
#define SYN_NUMBER      4		// number parser
#define SYN_PHRASE      5		// phrase generator
#define SYN_TRANS	6		// word is a foreign translation
#define SYN_UNKNOWN    15
#define SYN_INVALID   127

// bits
#define SYNBIT_SYNOMYM  0x0001
#define SYNBIT_STEM     0x0002
#define SYNBIT_SPELLING 0x0004
#define SYNBIT_ACRONYM  0x0008
#define SYNBIT_NUMBER   0x0010
#define SYNBIT_PHRASE   0x0020
#define SYNBIT_TRANS    0x0040
#define SYNBIT_UNKNOWN  0x8000
#define SYNBIT_STATIC   0x0049	// Synonym, acronym, translation
#define SYNBIT_DYNAMIC  0x0036  // stem, spelling, number, phrase
#define SYNBIT_ALL      0x7FFF

// TODO: Maybe make this a Msg class
class SynonymInfo {
private:
	bool growSyns();
	bool growText();
	bool growTids();
public:
	SynonymInfo();
	~SynonymInfo();

	void reset();

	bool setWord(char *s, long len, uint64_t h);

	bool addSynonym(char *syn, long affinity, 
			long offset, long len,
			char type, char sort, bool hasSpace, 
			int64_t leftWordId, int64_t rightWordId);

	char *m_word;
	uint64_t m_h;
	long m_wordLen;
	long m_numSyns;
	long m_numIds;	   // not 1:1 with syns
	long m_slots;
	char m_highSort;   // highest sort priority
	char **m_syn;
	long *m_affinity;
	long *m_offset;
	long *m_len;
	long *m_firstId;   // certain synonyms can have multiple termIds
	long *m_lastId;    // so this maps them, inclusive
	char *m_type;
	char *m_sort;	   // sort priority (lower = higher on the list)
	bool *m_hasSpace;
	int64_t *m_leftSynHash;  // for phrases, leftmost/rightmost syn hashes
	int64_t *m_rightSynHash;
	uint64_t *m_synHash;
	int64_t *m_termId; // for getTermFreqs (stored in its own buffer)
	int64_t m_tidBuf[16];
	long m_tidSize;
	char *m_balloc;
	long m_ballocSize;
	char m_buf[1024];
	char *m_talloc;
	long m_tallocSize;
	char m_tbuf[2048];
	long m_tbufLen;
};

// only used for rebuilding the thesaurus, not saved or used elsewhere,
//  but we need this for the HashTableT we use in rebuildSynonyms
//  m_syn here is actually offsets, if positive they were in dict/words, if
//  negative they were only in the add files
struct SynonymLinkGroup {
	long m_n;
	uint64_t m_h[MAX_SYNS];
	long m_syn[MAX_SYNS];
	char m_type[MAX_SYNS];
	long m_aff[MAX_SYNS];
};

class Thesaurus {
public:
	Thesaurus();
	~Thesaurus();

	void reset();

	// returns NULL if the offset does not point to the beginning of a word
	char *getSynonymFromOffset(long offset);

	// fills a SynonymInfo object with EVERYTHING
	bool getAllInfo(char *s, SynonymInfo *syn, long slen, long bits);

	// fills a SynonymInfo with static data
	bool getSynonymInfo(char *s, SynonymInfo *syn, 
			    long slen = 0, long bits = SYNBIT_ALL);
	bool getSynonymInfo(uint64_t h, SynonymInfo *syn,
			    long bits = SYNBIT_ALL);

	// all these functions are here for convienience/debugging but they're 
	//  terribly slow (O(n^2/2) if you use them iteratively), it's better 
	//  to use getSynonymInfo if you need to retrieve all the info at once
	// for all the N functions, N = 0 is the word itself, and N = numSyns
	//  is the least popular synonym
	long getAffinity(char *s1, char *s2, long l1 = 0, long l2 = 0);
	long getAffinity(uint64_t h1, uint64_t h2);
	long getAffinityN(char *s1, long n, long l = 0);
	long getAffinityN(uint64_t h1, long n);

	// maybe this is pointless, but it could be used to verify that one
	//  word is a synonym of another, if we decide we want to do that
	char *getSynonym(char *s1, char *s2, long l1 = 0, long l2 = 0);
	char *getSynonym(uint64_t h1, uint64_t h2);
	char *getSynonymN(char *s, long n, long l = 0);
	char *getSynonymN(uint64_t h, long n);

	long getNumSyns(char *s, long l = 0);
	long getNumSyns(uint64_t h);

	long getSlot(char *s1, char *s2, long l1 = 0, long l2 = 0);
	long getSlot(uint64_t h1, uint64_t h2);
	long getSlotN(char *s, long n, long l = 0);
	long getSlotN(uint64_t h, long n);

	long getOffset(char *s, long l = 0);
	long getOffset(uint64_t h);
	
	char getFlag(char *s1, char *s2, long l1 = 0, long l2 = 0);
	char getFlag(uint64_t h1, uint64_t h2);
	char getFlagN(char *s, long n, long l = 0);
	char getFlagN(uint64_t h, long n);

	int64_t getValue(char *s1, char *s2, long l1 = 0, long l2 = 0);
	int64_t getValue(uint64_t h1, uint64_t h2);
	int64_t getValueN(char *s, long n, long l = 0);
	int64_t getValueN(uint64_t h, long n);

	// cuts off and/or attaches suffixes to generate new words
	bool getStems(char *s, long slen, SynonymInfo *info);
	
	// turns a string/int into the other form ("2" -> "two" & vice versa)
	bool parseNumbers(long long n, SynonymInfo *syn);
	bool parseNumbers(char *s, long slen, SynonymInfo *syn);

	// generates new phrase synonyms off two-term phrases
	bool generatePhrases(char  *s, long slen, 
			     SynonymInfo *info, long bits);

	bool rebuild(char *server, bool fullRebuild);
	bool rebuildAffinity(char *server, bool fullRebuild);
	
	bool save();
	bool load();

	inline bool init() { 
		return load();
	}

	void cancelRebuild();

	StateAffinity *m_affinityState;	

	long m_lastRebuild;		// . gettimeofday() to force another
					//   full affinity rebuild
	bool m_rebuilding;		// is it currently rebuilding?
private:
	HashTableT<uint64_t, int64_t> m_synonymTable;
	char m_buf[2 * HT_BUF_SIZE];      // . synonym table buffer

	char *m_synonymText;
	long m_synonymLen;
	long m_synonymSize;
	long m_numSynonyms;
	long m_totalPairs;

	Suffix *m_suffixes;
	long m_numSuffixes;
	SafeBuf m_suffixBuffer;
	char **m_reps;
	long m_numReps;
	long *m_repLens;
	SafeBuf m_stemBuffer;
	HashTableT<unsigned long, char *> m_stemTable;

	bool initStems();
	bool rebuildSynonyms();

};

extern Thesaurus g_thesaurus;

#endif

