#include "gb-include.h"

#include "Thesaurus.h"
#include "HashTable.h"
#include "HttpServer.h"
#include "Dns.h"
#include "StopWords.h"
//#include "TitleRec.h" // for gb(un)compress
#include "Speller.h"
#include "Words.h"
#include "Bits.h"
#include "Phrases.h"
#include "sort.h"


/*
// returns false if fails and sets g_errno, true if succeeded
bool Thesaurus::load () {

	// open the thesaurus.txt file
	char filename[1024];
	sprintf(filename,"%sdict/thesaurus.txt",g_hostdb.m_dir);
	File f;
	f.open ( filename );
	// read it all in
	int32_t fsize = f.getFileSize();
	// alloc space
	char *buf = mmalloc(fsize,"thesaurusinit");
	if ( ! buf ) return false;
	// read it in
	int32_t n = f.read ( buf , fsize );
	// g_errno should be set in this case
	if ( n != fsize ) return false;
	
	char *p    = buf;
	char *pend = buf + fsize;

 loop:
	// skip til we hit '|'
	while ( p<pend && *p!='|' && *p!='\n') 
*/


// TODO: Add support for multiple languages ("dict/en/", "dict/de/", etc)

// stores synonym lists and affinity data
// when computing the affinity, order is important
// affinity is used to compute how often one word occurs with another
// affinity is computed as a ratio of list sizes
// (a,b) means size of list A to size of list (A intersect B)
// (b,a) means size of list B to size of list (A intersect B)

#define OFFSET(x) (x & 0x07FFFFFF)
#define AFFINITY(x) (x >> 32)
#define ISSYN(x) !(x & 0x80000000)
#define TYPE(x) ((x & 0x78000000) >> 27)

Thesaurus g_thesaurus;

// TODO: Replace this with a member variable at some point to support multiple
//  languages
static char *s_dictDir = "dict/";
static char *s_affFile = "thesaurus-affinity.txt";

#define MAX_STIDS 8

// quick and dirty
static int32_t findTermIds(char *s, int64_t *tids, 
			bool hasSpace, int32_t slen = 0) {
	static int64_t pid = 0;//getPrefixHash(NULL, 0, "", 0);
	char buf[256];
	if (!slen) slen = gbstrlen(s);
	if (slen > 255) return 0;
	gbmemcpy(buf, s, slen);
	buf[slen] = '\0';
	Words words;
	Bits bits;
	Phrases phrases;
	if (hasSpace) {
		// 0 for niceness
		words.set(buf, TITLEREC_CURRENT_VERSION, true, 0);
		bits.set(&words, TITLEREC_CURRENT_VERSION, 0);
		//spam.reset(words.getNumWords());
		phrases.set(&words, &bits, true, false, 
			TITLEREC_CURRENT_VERSION, 0);
		int32_t i = 0, j = 0;
		int64_t tid;
		while (i < words.getNumWords() && j < MAX_STIDS) {
			tid = phrases.getPhraseId2(i++);
			if (!tid) continue;
			tids[j++] = g_indexdb.getTermId(pid, tid);
		};
		return j;
	} else {
		words.set(buf, TITLEREC_CURRENT_VERSION, true, 0);
		tids[0] = g_indexdb.getTermId(pid, words.getWordId(0));
		return 1;
	}
}

bool isvowel(char c) {
	c = tolower(c);
	return  c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' ||
		c == 'h';
}

SynonymInfo::SynonymInfo() {
	m_ballocSize = 0;
	m_tallocSize = 0;
	m_tidSize = 0;
	reset();
}

// m_syn (char *), m_affinity, m_offset, m_len, m_firstId, m_numIds (int32_t),
// 	m_type, m_sort (char), m_hasSpace (bool), 
// 	m_leftSynHash, m_rightSynHash, m_synHash ((u)int64_t)
// m_termId is stored in a separate array due to it not necessarily being 1:1
static int32_t s_synSize = sizeof(char *) + sizeof(int32_t) * 5 + 
		      sizeof(char) * 2 + sizeof(int64_t) * 3 +
		      sizeof(bool);

void SynonymInfo::reset() {
	m_word = NULL;
	m_h = 0;
	m_wordLen = 0;
	m_numSyns = 0;
	m_numIds = 0;
	m_slots = sizeof(m_buf) / s_synSize;
	m_syn = NULL;
	m_affinity = NULL;
	m_offset = NULL;
	m_len = NULL;
	m_type = NULL;
	m_sort = NULL;
	m_hasSpace = NULL;
	m_leftSynHash = NULL;
	m_rightSynHash = NULL;
	m_synHash = NULL;
	if (m_tidSize) mfree(m_termId, m_tidSize, "SynonymTID");
	m_termId = m_tidBuf;
	m_tidSize = 0;
	if (m_ballocSize) mfree(m_balloc, m_ballocSize, "SynonymB");
	m_balloc = m_buf;
	m_ballocSize = 0;
	if (m_tallocSize) mfree(m_talloc, m_tallocSize, "SynonymT");
	m_talloc = m_tbuf;
	m_tallocSize = 0;
	m_tbufLen = 0;
}

SynonymInfo::~SynonymInfo() {
	reset();
}

bool SynonymInfo::growSyns() {
	int32_t newSize = 0;
	char *newBuf;
	char **newSyn;
	int32_t *newAffinity, *newOffset, *newLen, *newFirstId, *newLastId;
	char *newType, *newSort;
	bool *newHasSpace;
	int64_t *newLeftId, *newRightId;
	uint64_t *newSynHash;
	char *p;
	int32_t newSlots;
	if (!m_ballocSize) newSize = sizeof(m_buf) * 2;
	else               newSize = m_ballocSize + sizeof(m_buf);
	newBuf = (char *)mmalloc(newSize, "SynonymB");
	if (!newBuf) return false;
	newSlots = newSize / s_synSize;
	p = newBuf;
	newSyn      = (char **)  p; p += newSlots * sizeof(char *);
	newAffinity = (int32_t *)   p; p += newSlots * sizeof(int32_t);
	newOffset   = (int32_t *)   p; p += newSlots * sizeof(int32_t);
	newLen      = (int32_t *)   p; p += newSlots * sizeof(int32_t);
	newFirstId  = (int32_t *)   p; p += newSlots * sizeof(int32_t);
	newLastId   = (int32_t *)   p; p += newSlots * sizeof(int32_t);
	newType     =            p; p += newSlots * sizeof(char);
	newSort     =            p; p += newSlots * sizeof(char);
	newHasSpace = (bool *)   p; p += newSlots * sizeof(bool);
	newLeftId   = (int64_t *)p; p += newSlots * sizeof(int64_t);
	newRightId  = (int64_t *)p; p += newSlots * sizeof(int64_t);
	newSynHash  = (uint64_t*)p; p += newSlots * sizeof(uint64_t);
	gbmemcpy(newSyn     , m_syn     , m_numSyns * sizeof(char *));
	gbmemcpy(newAffinity, m_affinity, m_numSyns * sizeof(int32_t));
	gbmemcpy(newOffset  , m_offset  , m_numSyns * sizeof(int32_t));
	gbmemcpy(newLen     , m_len     , m_numSyns * sizeof(int32_t));
	gbmemcpy(newFirstId , m_firstId , m_numSyns * sizeof(int32_t));
	gbmemcpy(newLastId  , m_lastId  , m_numSyns * sizeof(int32_t));
	gbmemcpy(newType    , m_type    , m_numSyns * sizeof(char));
	gbmemcpy(newSort    , m_sort    , m_numSyns * sizeof(char));
	gbmemcpy(newHasSpace, m_hasSpace, m_numSyns * sizeof(bool));
	gbmemcpy(newLeftId  , m_leftSynHash  , m_numSyns * sizeof(int64_t));
	gbmemcpy(newRightId , m_rightSynHash , m_numSyns * sizeof(int64_t));
	gbmemcpy(newSynHash , m_synHash , m_numSyns * sizeof(uint64_t));
	m_syn = newSyn;
	m_affinity = newAffinity;
	m_offset = newOffset;
	m_len = newLen;
	m_firstId = newFirstId;
	m_lastId = newLastId;
	m_type = newType;
	m_sort = newSort; 
	m_hasSpace = newHasSpace;
	m_leftSynHash = newLeftId;
	m_rightSynHash = newRightId;
	m_synHash = newSynHash;
	if (m_ballocSize) mfree(m_balloc, m_ballocSize, "SynonymB");
	m_balloc = newBuf;
	m_ballocSize = newSize;
	return true;
}

bool SynonymInfo::growText() {
	int32_t newSize = m_tallocSize + sizeof(m_tbuf) * 2;
	char *newBuf;
	newBuf = (char *)mmalloc(newSize, "SynonymT");
	if (!newBuf) return false;
	for (int32_t i = 0; i < m_numSyns; i++) {
		m_syn[i] = newBuf + (m_syn[i] - m_talloc);
	}
	gbmemcpy(newBuf, m_talloc, m_tbufLen);
	if (m_tallocSize) mfree(m_talloc, m_tallocSize, "SynonymT");
	m_talloc = newBuf;
	m_tallocSize = newSize;
	return true;
}

bool SynonymInfo::growTids() {
	int32_t newSize;
	if (!m_tidSize) {
		newSize = sizeof(m_tidBuf) + sizeof(int64_t) * MAX_STIDS;
	} else {
		newSize = m_tidSize + sizeof(int64_t) * MAX_STIDS;
	}
	int64_t *newBuf;
	newBuf = (int64_t *)mcalloc(newSize, "SynonymTID");
	if (!newBuf) return false;
	gbmemcpy(newBuf, m_termId, m_tidSize);//newSize);
	if (m_tidSize > (int32_t)sizeof(m_tidBuf)) {
		mfree(m_termId, m_tidSize, "SynonymTID");
	}
	m_termId = newBuf;
	m_tidSize = newSize;
	return true;
}

bool SynonymInfo::setWord(char *s, int32_t len, uint64_t h) {
	// theoretically we shouldn't need this, but it's safer
	int32_t tbufSize = m_tallocSize;
	if (!tbufSize) tbufSize = sizeof(m_tbuf);
	if ((len + m_tbufLen > tbufSize) && !growText()) {
		return log("query: ran out of memory producing synonyms");
	}
	gbmemcpy(m_talloc, s, len);
	m_h = h;
	return true;
}

bool SynonymInfo::addSynonym(char *syn, int32_t affinity,
			     int32_t offset, int32_t len,
			     char type, char sort, bool hasSpace,
			     int64_t leftSynHash, int64_t rightSynHash) {
	int32_t bufSize = m_ballocSize;
	int32_t tbufSize = m_tallocSize;
	int32_t tidSize = m_tidSize;
	int32_t bufNeed = (m_numSyns + 1) * s_synSize;

	// check for duplicates
	uint64_t h = hash64Lower_utf8(syn, len);
	if (h == m_h) {
		return log(LOG_DEBUG, "query: Synonym dup hash %016"XINT64"", m_h);
	}

	int64_t tids[MAX_STIDS];
	int32_t addIds = findTermIds(syn, tids, hasSpace, len);
	int32_t tidNeed = (m_numIds + addIds) * sizeof(int64_t);

	int32_t i, j;
	

	// check for duplicates
	for (i = 0; i < m_numSyns; i++) {
		// if the number of ids is different, definitely not a match
		//  prevents false positive "sled dog" and "sled dog_iron" 
		if (m_lastId[i] - m_firstId[i] + 1 != addIds) continue;	
		for (j = m_firstId[i]; j <= m_lastId[i]; j++) {
			int k = j - m_firstId[i];
			if (m_termId[j] != tids[k]) break; // mismatch
		}
		if (j <= m_lastId[i]) continue; // mismatch, check next one
		return log(LOG_DEBUG, "query: Synonym dup by tids %"INT32"", i);
	}

	// grow the buffers if need be
	if (!bufSize) bufSize = sizeof(m_buf);
	if (!tbufSize) tbufSize = sizeof(m_tbuf);
	if (!tidSize) tidSize = sizeof(m_tidBuf);
	if (((bufNeed > bufSize) && !growSyns()) ||
	    ((m_tbufLen + len > tbufSize) && !growText()) || 
	    ((tidNeed > tidSize) && !growTids())) {
		return log("query: ran out of memory producing synonyms");
	}
	
	// assign pointers if necessary
	if (!m_syn) {
		int32_t slots = bufSize / s_synSize;
		char *p = m_buf;
		m_syn      = (char **)  p; p += slots * sizeof(char *);
		m_affinity = (int32_t *)   p; p += slots * sizeof(int32_t);
		m_offset   = (int32_t *)   p; p += slots * sizeof(int32_t);
		m_len      = (int32_t *)   p; p += slots * sizeof(int32_t);
		m_firstId  = (int32_t *)   p; p += slots * sizeof(int32_t);
		m_lastId   = (int32_t *)   p; p += slots * sizeof(int32_t);
		m_type     =            p; p += slots * sizeof(char);
		m_sort     =            p; p += slots * sizeof(char);
		m_hasSpace = (bool *)   p; p += slots * sizeof(bool);
		m_leftSynHash   = (int64_t *)p; p += slots * sizeof(int64_t);
		m_rightSynHash  = (int64_t *)p; p += slots * sizeof(int64_t);
		m_synHash  = (uint64_t*)p; p += slots * sizeof(uint64_t);
	}

	// check the sort
	if (m_highSort < sort) {
		m_highSort = sort;
	}

	// and finally, load all the info into the structure
	gbmemcpy(m_talloc + m_tbufLen, syn, len);
	m_syn[m_numSyns] = m_talloc + m_tbufLen;
	m_tbufLen += len;
	m_affinity[m_numSyns] = affinity;
	m_offset[m_numSyns] = offset;
	m_firstId[m_numSyns] = m_numIds;
	m_lastId[m_numSyns] = m_numIds + addIds - 1;
	m_len[m_numSyns] = len;
	m_type[m_numSyns] = type;
	m_sort[m_numSyns] = sort;
	m_hasSpace[m_numSyns] = hasSpace;
	m_leftSynHash[m_numSyns] = leftSynHash;
	m_rightSynHash[m_numSyns] = rightSynHash;
	m_synHash[m_numSyns] = h;
	m_numSyns++;
	for (i = 0; i < addIds; i++) {
		m_termId[m_numIds++] = tids[i];
	}
	return true;
}

Thesaurus::Thesaurus() {
	m_rebuilding = false;
	m_affinityState = NULL;
	m_synonymTable.reset();
	m_synonymTable.set(0, m_buf, 2 * HT_BUF_SIZE, true);
	m_synonymText = NULL;
	m_synonymLen = 0;
	m_synonymSize = 0;
	m_numSynonyms = 0;
	m_totalPairs = 0;
}

Thesaurus::~Thesaurus() {
	reset();
	m_synonymTable.reset();
}

void Thesaurus::reset() {
	if (m_reps) {
		mfree(m_reps, sizeof(char *) * m_numReps, "stemmer");
	}
	if (m_repLens) {
		mfree(m_repLens, sizeof(int32_t) * m_numReps, "stemmer");
	}
	m_reps = NULL;
	m_repLens = NULL;
	m_numReps = 0;
	if (m_suffixes) {
		mfree(m_suffixes, sizeof(Suffix) * m_numSuffixes, "stemmer");
	}
	m_suffixes = NULL;
	m_numSuffixes = 0;
	m_suffixBuffer.reset();
	m_stemTable.reset();
	m_stemTable.set(0, 0, 0, true);
	m_stemBuffer.reset();
	m_rebuilding = false;
	// note that we DON'T reset affinityState here, this needs to be
	//  cleaned up by the affinity code so we can detect that there's
	//  outstanding requests to deal with still
	//m_synonymTable.reset();
	//m_synonymTable.set(0, m_buf, 2 * HT_BUF_SIZE, true);
	if (m_synonymText) mfree(m_synonymText, m_synonymSize, "thesaurus");
	m_synonymText = NULL;
	m_synonymLen = 0;
	m_synonymSize = 0;
	m_numSynonyms = 0;
	m_totalPairs = 0;
}

bool Thesaurus::rebuild(char *server, bool fullRebuild) {
	log(LOG_INFO, "build: rebuilding Thesaurus synonyms");
	if (!rebuildSynonyms()) {
		log("build: Couldn't rebuild Thesaurus synonyms");
		return true;
	}
	if (!load()) {
		log("build: Couldn't load rebuilt thesaurus data, disk "
			"problem?");
		return true;
	}
	// this function starts a callback loop, returns true on error
	return rebuildAffinity(server, fullRebuild);
}

void Thesaurus::cancelRebuild() {
	m_rebuilding = false;
}

char *Thesaurus::getSynonymFromOffset(int32_t offset) {
	// corner cases first
	if (offset == 0) return m_synonymText;
	if (offset >= m_synonymLen || offset < 0) return NULL;
	// if the character just before the offset is a null byte, we're at
	//  the beginning of a word, so as int32_t as the rest of the code is
	//  sane this is valid
	if (m_synonymText[offset-1] == '\0') return m_synonymText + offset;
	// otherwise, no, we're in the middle of a word and this isn't valid
	return NULL;
}


bool Thesaurus::getAllInfo(char *s, SynonymInfo *info, int32_t slen,
			   int32_t bits) {
	bool r = false;
	if (!slen) slen = gbstrlen(s);
	if (slen > 256) return false;
	if (!bits) return false;
	log(LOG_DEBUG, "query: getAllInfo(%32s, %"INT32", %p, %"XINT32")", 
		s, slen, info, bits);
	// do stems first so SYN_STEM overrides
	if (bits & SYNBIT_STEM) r |= getStems(s, slen, info);
	r |= getSynonymInfo(s, info, slen, bits);
	/*
	  MDW: take this out until it works!
	if (bits & SYNBIT_SPELLING) {
		bool found;
		int32_t score, popularity;
		char buf[256];
		if (g_speller.m_language[langEnglish].getRecommendation(
			s, slen, buf, 256, 
			&found, &score, &popularity, false) &&
		    buf[0] ) {
			r |= info->addSynonym(buf, DEF_AFFINITY, -1, 
					      gbstrlen(buf), SYN_SPELLING,
					      strchr(buf, ' ') != NULL, 
					      0, 0);
		}
	}
	*/
	if (bits & SYNBIT_NUMBER) r |= parseNumbers(s, slen, info);
	if (bits & SYNBIT_PHRASE) r |= generatePhrases(s, slen, info, bits);
	return r;
}

bool Thesaurus::getSynonymInfo(char *s, SynonymInfo *info, 
			       int32_t slen, int32_t bits) {
	if (!slen) slen = gbstrlen(s);
	uint64_t h = hash64Lower_utf8(s, slen);
	// debug
	log(LOG_DEBUG,"query: get syn info for %s",s);
	// do not get synonyms of stop words...
	//if ( ! isStopWord ( s , gbstrlen(s) , h ) ) 
	// this returns true if we found some synonyms
	bool r = getSynonymInfo(h, info, bits);
	if (!r) info->setWord(s, slen, h);
	return r;
}

bool Thesaurus::getSynonymInfo(uint64_t h, SynonymInfo *info, int32_t bits) {
	int32_t slot = m_synonymTable.getSlot(h);
	if (slot < 0) return false;
	log(LOG_DEBUG, "query: getSynonymInfo(%"XINT64", %p, %"XINT32")", h, info, bits);
	// this is NOW the first synonym
	//char *p = m_synonymText + OFFSET(m_synonymTable.
	//				    getValueFromSlot(slot));
	//int32_t len = gbstrlen(p);
	//info->setWord(p, len, h);
	info->setWord(NULL,0,h);
	do {
		if (m_synonymTable.getKey(slot) == h) {
			int64_t v = m_synonymTable.getValueFromSlot(slot);
			int32_t o = OFFSET(v);
			char *p = m_synonymText + o;
			int32_t a = AFFINITY(v);
			int32_t t = TYPE(v);
			bool sp = strchr(p, ' ') != NULL;
			char sr;
			//if (!a) continue;
			if (t == SYN_STEM) sr = 1;
			else               sr = 2;
			if (bits & (1 << t)) {
				info->addSynonym(p, a, o, gbstrlen(p), t, sr,
						 sp, 0, 0);
			}
			
		}
		if (++slot >= m_synonymTable.getNumSlots()) slot = 0;
	} while (m_synonymTable.getKey(slot));
	return true;
}

int32_t Thesaurus::getAffinity(char *s1, char *s2, int32_t l1, int32_t l2) {
	if (!l1) l1 = gbstrlen(s1);
	if (!l2) l2 = gbstrlen(s2);
	return getAffinity(hash64Lower_utf8(s1, l1), hash64Lower_utf8(s2, l2));
}

int32_t Thesaurus::getAffinity(uint64_t h1, uint64_t h2) {
	if (h1 == h2) return MAX_AFFINITY;
	int32_t slot = m_synonymTable.getSlot(h1);
	if (slot < 0) return -1;
	while (m_synonymTable.getKey(slot)) {
		if (m_synonymTable.getKey(slot) == h1) {
			int64_t v = m_synonymTable.getValueFromSlot(slot);
			char *p = m_synonymText + OFFSET(v);
			if (h2 == hash64Lower_utf8(p, gbstrlen(p))) 
				return AFFINITY(v);
		}
		if (++slot >= m_synonymTable.getNumSlots()) slot = 0;
	}
	return -1;
}

int32_t Thesaurus::getAffinityN(char *s, int32_t n, int32_t l) {
	if (!l) l = gbstrlen(s);
	return getAffinityN(hash64Lower_utf8(s, l), n);
}

int32_t Thesaurus::getAffinityN(uint64_t h, int32_t n) {
	int32_t slot = m_synonymTable.getSlot(h);
	if (slot < 0) return -1;
	while (m_synonymTable.getKey(slot)) {
		if (m_synonymTable.getKey(slot) == h && !n--)  
		 	return AFFINITY(m_synonymTable.getValueFromSlot(slot));
		if (++slot >= m_synonymTable.getNumSlots()) slot = 0;
	}
	return -1;
}

char *Thesaurus::getSynonymN(char *s, int32_t n, int32_t l) {
	if (!l) l = gbstrlen(s);
	return getSynonymN(hash64Lower_utf8(s, l), n);
}

char *Thesaurus::getSynonymN(uint64_t h, int32_t n) {
	int32_t slot = m_synonymTable.getSlot(h);
	if (slot < 0) return NULL;
	while(m_synonymTable.getKey(slot)) {
		if (m_synonymTable.getKey(slot) == h && !n--)
			return m_synonymText + 
				OFFSET(m_synonymTable.getValueFromSlot(slot));
		if (++slot >= m_synonymTable.getNumSlots()) slot = 0;
	}
	return NULL;
}

int32_t Thesaurus::getNumSyns(char *s, int32_t l) {
	if (!l) l = gbstrlen(s);
	return getNumSyns(hash64Lower_utf8(s, gbstrlen(s)));
}

int32_t Thesaurus::getNumSyns(uint64_t h) {
	int32_t slot = m_synonymTable.getSlot(h);
	if (slot < 0) return 0;
	++slot;		// skip the first slot
	int32_t r = 0;
	while(m_synonymTable.getKey(slot)) { 
		if (m_synonymTable.getKey(slot) == h) r++;
		if (++slot >= m_synonymTable.getNumSlots()) slot = 0;
	}
	return r;
}

int32_t Thesaurus::getSlot(char *s1, char *s2, int32_t l1, int32_t l2) {
	if (!l1) l1 = gbstrlen(s1);
	if (!l2) l2 = gbstrlen(s2);
	return getSlot(hash64Lower_utf8(s1, l1), hash64Lower_utf8(s2, l2));
}

int32_t Thesaurus::getSlot(uint64_t h1, uint64_t h2) {
	int32_t slot = m_synonymTable.getSlot(h1);
	if (slot < 0) return -1;
	if (h1 == h2) return slot;
	while(m_synonymTable.getKey(slot)) {
		if (m_synonymTable.getKey(slot) == h1) {
			char *p = m_synonymText + 
				OFFSET(m_synonymTable.getValueFromSlot(slot));
			if (hash64Lower_utf8(p, gbstrlen(p)) == h2) return slot;
		}
		if (++slot >= m_synonymTable.getNumSlots()) slot = 0;
	}
	return -1;
}

int32_t Thesaurus::getSlotN(char *s, int32_t n, int32_t l) {
	if (!l) l = gbstrlen(s);
	return getSlotN(hash64Lower_utf8(s, l), n);
}

int32_t Thesaurus::getSlotN(uint64_t h, int32_t n) {
	int32_t slot = m_synonymTable.getSlot(h);
	if (slot < 0) return -1;
	while(m_synonymTable.getKey(slot)) {
		if (m_synonymTable.getKey(slot) == h && !n--) return slot;
		if (++slot >= m_synonymTable.getNumSlots()) slot = 0;
	}
	return -1;
}

int32_t Thesaurus::getOffset(char *s, int32_t l) {
	if (!l) l = gbstrlen(s);
	return getOffset(hash64Lower_utf8(s));
}

int32_t Thesaurus::getOffset(uint64_t h) {
	int32_t slot = m_synonymTable.getSlot(h);
	if (slot < 0) return -1;
	return OFFSET(m_synonymTable.getValueFromSlot(slot));
}

char Thesaurus::getFlag(char *s1, char *s2, int32_t l1, int32_t l2) {
	if (!l1) l1 = gbstrlen(s1);
	if (!l2) l2 = gbstrlen(s2);
	return getFlag(hash64Lower_utf8(s1, l1), hash64Lower_utf8(s2, l2));
}

char Thesaurus::getFlag(uint64_t h1, uint64_t h2) {
	int32_t slot = m_synonymTable.getSlot(h1);
	if (slot < 0) return SYN_INVALID;
	while(m_synonymTable.getKey(slot)) {
		if (m_synonymTable.getKey(slot) == h1) {
			int64_t v = m_synonymTable.getValueFromSlot(slot);
			char *p = m_synonymText + OFFSET(v);
			if (hash64Lower_utf8(p) == h2) return TYPE(v);
		}
		if (++slot >= m_synonymTable.getNumSlots()) slot = 0;
	}
	return SYN_INVALID;
}

char Thesaurus::getFlagN(char *s, int32_t n, int32_t l) {
	if (!l) l = gbstrlen(s);
	return getFlagN(hash64Lower_utf8(s, l), n);
}

char Thesaurus::getFlagN(uint64_t h, int32_t n) {
	int32_t slot = m_synonymTable.getSlot(h);
	if (slot < 0) return SYN_INVALID;
	while(m_synonymTable.getKey(slot)) {
		if (m_synonymTable.getKey(slot) == h && !n--)
			return TYPE(m_synonymTable.getValueFromSlot(slot));
		if (++slot >= m_synonymTable.getNumSlots()) slot = 0;
	}
	return SYN_INVALID;
}

int64_t Thesaurus::getValue(char *s1, char *s2, int32_t l1, int32_t l2) {
	if (!l1) l1 = gbstrlen(s1);
	if (!l2) l2 = gbstrlen(s2);
	return getValue(hash64Lower_utf8(s1, l1), hash64Lower_utf8(s2, l2));
}

int64_t Thesaurus::getValue(uint64_t h1, uint64_t h2) {
	int32_t slot = m_synonymTable.getSlot(h1);
	if (slot < 0) return -1LL;
	while(m_synonymTable.getKey(slot)) {
		if (m_synonymTable.getKey(slot) == h1) {
			int64_t v = m_synonymTable.getValueFromSlot(slot);
			char *p = m_synonymText + OFFSET(v);
			if (hash64Lower_utf8(p) == h2) return v;
		}
		if (++slot >= m_synonymTable.getNumSlots()) slot = 0;
	}
	return -1LL;
}

int64_t Thesaurus::getValueN(char *s, int32_t n, int32_t l) {
	if (!l) l = gbstrlen(s);
	return getValueN(hash64Lower_utf8(s, l), n);
}

int64_t Thesaurus::getValueN(uint64_t h, int32_t n) {
	int32_t slot = m_synonymTable.getSlot(h);
	if (slot < 0) return -1LL;
	while(m_synonymTable.getKey(slot)) {
		if (m_synonymTable.getKey(slot) == h && !n--)
			return m_synonymTable.getValueFromSlot(slot);
		if (++slot >= m_synonymTable.getNumSlots()) slot = 0;
	}
	return -1LL;
}

static int removePunctuation(char *src, int srcLen) {
	int i = 0, j = 0;
	while (i < srcLen) {
		if (src[i] != '-' && src[i] != '.' && src[i] != ',') 
			src[j++] = src[i];
		i++;
	}
	return j;
}

bool Thesaurus::getStems(char *s, int32_t slen, SynonymInfo *info) {
	int32_t lang = 1;		// FIXME: add support for other languages
	if (slen > 255) return false;
	if (!m_suffixes) return false;
	bool r = false;
	char s2[256];
	bool dbl = false;	// double consonant?

	// sanity check - for to_lower_utf8 to work
	if ( s[slen] ) { char *xx=NULL;*xx=0; }

	// store it as a lower case string into "s2"
	to_lower_utf8(s2 , s2+250, s);

	uint32_t h = hash32Lower_utf8(s, slen);

	// do not do this on stop words!
	if ( isStopWord ( s , slen , h ) )
		return false;

	int32_t slot = m_stemTable.getSlot(h);
	// check for exceptions
	if (slot >= 0) {
		char *p = m_stemTable.getValueFromSlot(slot);
		int32_t plen = gbstrlen(p);
		if (p[0] != '.') {
			r |= info->addSynonym(p, -1, -1, plen,
					      SYN_STEM, 1, false, 
					      0, 0);
		}
		do {
			if (++slot >= m_synonymTable.getNumSlots()) slot = 0;
			if (m_synonymTable.getKey(slot) == h) {
				p = m_stemTable.getValueFromSlot(slot);
				plen = gbstrlen(p);
				if (p[0] == '.') continue;
				r |= info->addSynonym(p, -1, -1, plen, 
						      SYN_STEM, 1, false, 
						      0, 0);
			}
		} while (m_stemTable.getKey(slot));
	}

	char buf[256], buf2[256];
	int32_t bufLen = 0, buf2Len = 0;
	// see if we can remove punctuation first
	gbmemcpy(buf, s2, slen);
	bufLen = removePunctuation(buf, slen);
	if (bufLen != slen) {
		r |= info->addSynonym(buf, -1, -1, bufLen, 
				      SYN_STEM, 1, false, 0, 0);
	}

	Suffix *suf = m_suffixes;
	Suffix *sufend = m_suffixes + m_numSuffixes;
	int32_t sufLen;
	for (; suf < sufend; suf++) {
		sufLen = suf->m_len;
		// if replacing the suffix would int16_ten the word below 3,
		// skip it
		if (sufLen >= slen - 1) continue;
		if (!memcmp(s2 + slen - sufLen, suf->m_suffix, sufLen)) break;
	}

	char **rep;
	char **repend;
	int32_t *repLenp, repLen;
	int32_t best = -1;

	// found a usable suffix, so try to stem it
	if (suf != sufend) {
		rep = suf->m_reps;
		repend = suf->m_reps + suf->m_numReps;
		repLenp = suf->m_repLens;
		// find the most likely word

		while (rep < repend) {
			gbmemcpy(buf, s2, slen);
			repLen = *repLenp;
			int32_t stemLen = slen - sufLen;
			bool mdbl = false;
			bufLen = stemLen + repLen;
			if (bufLen <= 1) continue;
			// attach the replacement
			gbmemcpy(buf + stemLen, *rep, repLen + 1);
			rep++;
			repLenp++;
			// needs to be hash64d because that's what the speller
			//  is expecting
			uint64_t h2 = hash64d(buf, bufLen);
			int32_t pop = g_speller.getPhrasePopularity(buf, h2, 
								 false, lang);
			if (g_conf.m_logDebugQuery) {
				char buf3[256];
				gbmemcpy(buf3, buf, bufLen);
				buf3[bufLen] = '\0';
				log(LOG_DEBUG, "query: maybe stem %s (%"INT32")", 
					buf3, pop);
			}
			// if the replacement is empty, see if removing a
			//  double-consanant is a better match
			//  note that some other code assumes this only
			//  happens if the replacement is empty so if you
			//  change this please change the other code as well
			//  - bcc
			if (!repLen && stemLen > 1 && 
				buf[stemLen-1] == buf[stemLen-2]) {
				char buf3[256];
				gbmemcpy(buf3, buf, bufLen - 1);
				buf3[bufLen - 1] = '\0';
				h2 = hash64d(buf3, bufLen - 1);
				int32_t pop2 = g_speller.getPhrasePopularity(
						buf3, h2, false, lang);
				if (pop2 > pop) {
					log(LOG_DEBUG, "query: Double "
						"consonant removed \"%s\""
						" (%"INT32")",
						buf3, pop2);
					gbmemcpy(buf, buf3, bufLen);
					pop = pop2;
					bufLen--;
					mdbl = true;
				}
			}
			if (!pop) continue;
			if (best < pop) {
				best = pop;
				gbmemcpy(buf2, buf, bufLen + 1);
				buf2Len = bufLen;
				dbl = mdbl;
			}
		}
	}

	// if we found something, add it in
	if (best >= 0) {
		log(LOG_DEBUG, "query: Stemming %s to %s (%"INT32")", 
			s, buf2, best);
		r |= info->addSynonym(buf2, -1, -1, buf2Len, 
				      SYN_STEM, 1, false, 0, 0);
	} else {
		// else just copy this in to make the next section simpler
		gbmemcpy(buf2, s2, slen);
		buf2Len = slen;
	}
	
	// loop through all the other suffixes and see if we can
	//  attach them and get a useable word
	suf = m_suffixes;

	while (suf < sufend) {
		rep = suf->m_reps;
		repend = suf->m_reps + suf->m_numReps;
		repLenp = suf->m_repLens;
		while (rep < repend) {
			repLen = *repLenp;
			char *rep2 = *rep;
			char buf3[256];
			int32_t buf3Len;
			int32_t pop2 = 0;
			rep++;
			repLenp++;
			if (memcmp(buf2 + buf2Len - repLen, rep2, repLen))
				continue;
			// found a possible replacement, so add the
			//  suffix to it and see what we get
			gbmemcpy(buf, buf2, buf2Len);
			bufLen = buf2Len - repLen;
			gbmemcpy(buf + bufLen, suf->m_suffix, suf->m_len + 1);
			bufLen += suf->m_len;
			// needs to be hash64d because that's what the speller
			//  is expecting
			uint64_t h2 = hash64d(buf, bufLen);
			int32_t pop = g_speller.getPhrasePopularity(buf, h2, 
								 false, lang);
			// if we removed a double consonant, add it back and
			//  evaluate it with the new suffix
			if (dbl) {
				// if we reached here, repLen is always 0
				gbmemcpy(buf3, buf2, bufLen);
				buf3[buf2Len] = buf3[buf2Len - 1];
				gbmemcpy(buf3 + buf2Len + 1, suf->m_suffix,
					suf->m_len + 1);
				buf3Len = buf2Len + 1 + suf->m_len;
				h2 = hash64d(buf3, buf3Len);
				pop2 = g_speller.getPhrasePopularity(buf3, h2, 
								 false, lang);
			}
			if (pop) { // got a potential suffix
				log(LOG_DEBUG, "query: adding unstem \"%s\" "
					"%"INT32"", buf, pop);
				r |= info->addSynonym(buf, -1, -1, bufLen,
					         SYN_STEM, 1, false, 0, 0);
			}
			if (pop2) {
				log(LOG_DEBUG, "query: adding unstem \"%s\" "
					"%"INT32"", buf3, pop);
				r |= info->addSynonym(buf3, -1, -1, bufLen + 1,
						 SYN_STEM, 1, false, 0, 0);
			}
		}
		suf++;
	}

	return r;
}

struct Number {
	char *m_word;
	int64_t m_number;
	int32_t m_len;
	uint32_t m_h;
};

static Number s_smallNumbers[] = {
	{ "zero",       0 },
	{ "one",        1 },
	{ "two",        2 },
	{ "three",      3 },
	{ "four",       4 },
	{ "five",       5 },
	{ "six",        6 },
	{ "seven",      7 },
	{ "eight",      8 },
	{ "nine" ,      9 },
	{ "ten",       10 },
	{ "eleven",    11 },
	{ "twelve",    12 },
	{ "thirteen",  13 },
	{ "fourteen",  14 },
	{ "fifteen",   15 },
	{ "sixteen",   16 },
	{ "seventeen", 17 },
	{ "eighteen",  18 },
	{ "nineteen",  19 },
	{ 0 }
};

static Number s_tens[] = {
	{ "twenty",   20 },
	{ "thirty",   30 },
	{ "forty",    40 },
	{ "fifty",    50 },
	{ "sixty",    60 },
	{ "seventy",  70 },
	{ "eighty",   80 },
	{ "ninety",   90 },
	{ "hundred", 100 },
	{ 0 }
};

static Number s_bigNumbers[] = {
	{ "quintillion", 1000000000000000000LL },
	{ "quadrillion", 1000000000000000LL },
	{ "trillion",    1000000000000LL },
	{ "billion",     1000000000 },
	{ "million",     1000000 },
	{ "thousand",    1000 },
	{ 0 }
};

// TODO: Make this work again when other number systems are added in
#if 0
void testNumber() {
	SynonymInfo info;
	for(int i = 0; i < 10000; i++) {
		int64_t n1 = (int64_t)rand() << 32 + rand();
		bool r1 = g_thesaurus.parseNumbers(n1, &info);
		if (!r1) log(LOG_INFO, "query: %lld failure", n1);
		bool r2 = g_thesaurus.parseNumbers(info.m_syn[0], 
						   info.m_len[0], 
						   &info);
		int64_t n2 = strtoll(info.m_, 0, 0);
		if (n1 != n2 && n1 >= 0) 
			log(LOG_INFO, "query: %lld %lld %s", n1, n2, buf1);
	}
}
#endif

bool Thesaurus::parseNumbers(int64_t n, SynonymInfo *info) {
	SafeBuf buf(256);
	bool sp = false, r = true;
	// break it down until there's nothing left
	while (n > 0) {
		Number *mult = NULL;
		for (Number *number = s_bigNumbers; number->m_word;
			number++) {
			if (n >= number->m_number) {
				mult = number;
				break;
			}
		}
		int64_t base = n;
		if (mult) base /= mult->m_number;
		int32_t hundred = base / 100;
		int32_t tens;
		int32_t small;
		if (base % 100 < s_tens[0].m_number) {
			tens = 0;
			small = base % 100;
		} else {
			tens = (base % 100) / 10;
			small = base % 10;
		}
		if (hundred) {
			sp = true;
			r &= buf.safePrintf("%s hundred", 
				s_smallNumbers[hundred].m_word);
			if (tens || small) 
				r &= buf.safePrintf(" ");
		}
		if (tens) {
			r &= buf.safePrintf("%s", s_tens[tens-2].m_word);
			if (small) {
				sp = true;
				r &= buf.safePrintf(" ");
			}
		}
		if (small) r &= buf.safePrintf("%s", 
					s_smallNumbers[small].m_word);
		if (mult) {
			sp = true;
			r &= buf.safePrintf(" %s", mult->m_word);
			n %= mult->m_number;
			if (n >= 100) { r &= buf.safePrintf(", "); }
			else if (n)   { r &= buf.safePrintf(" and "); }
		} else {
			n = 0;
		}
	}
	if (!buf.length() && n == 0) r &= buf.safePrintf("zero");
	if (buf.length() && r) {
		r &= info->addSynonym(buf.getBufStart(), -1, -1, 
				      buf.length(), SYN_NUMBER, 2, sp, 0, 0);
	}
	return r;
}

bool Thesaurus::parseNumbers(char *s, int32_t slen, SynonymInfo *info) {
	// TODO: Make this language specific
	// init the hashes if they don't exist yet
	if (!s_smallNumbers[0].m_len)
		for (Number *number = s_smallNumbers; number->m_word; 
			number++) {
			number->m_len = gbstrlen(number->m_word);
			number->m_h = hash32n(number->m_word);
		}
	if (!s_tens[0].m_len)
		for (Number *number = s_tens; number->m_word; 
			number++) {
			number->m_len = gbstrlen(number->m_word);
			number->m_h = hash32n(number->m_word);
		}
	if (!s_bigNumbers[0].m_len)
		for (Number *number = s_bigNumbers; number->m_word; 
			number++) {
			number->m_len = gbstrlen(number->m_word);
			number->m_h = hash32n(number->m_word);
		}
	if (!slen) slen = gbstrlen(s);
	// first check to see if we have digits
	char *p = s, *pend = s + slen;
	char *send;
	int64_t n = strtoll(s, &send, 10);
	if (s != send && send == pend) {
		return parseNumbers(n, info);
	}
	SafeBuf buf(256);
	n = 0;
	int64_t m = 0;
	Number *sm = NULL, *tn = NULL, *hn = NULL, *md = NULL;
	while (p < pend) {
		while ((isspace(*p) || *p == ',') && p < pend) p++;
		char *sp = p;
		while (!isspace(*sp) && *sp != ',' && sp < pend) sp++;
		if ( sp - p > 3 && !strncmp(p, "and", sp - p)) { 
			p = sp;
			continue;
		}
		uint32_t h = hash32(p, sp - p);
		bool match = false;
		if (!md) for (Number *number = s_bigNumbers; 
			number->m_word; number++) {
			if (h == number->m_h && sp - p == number->m_len) {
				match = true;
				md = number;
				if (!sm && !tn && !hn) 
				{ sm = s_smallNumbers + 1; }
				break;
			}
		}
		if ((!hn || !tn) && !match) for (Number *number = s_tens; 
			number->m_word; number++) {
			if (h == number->m_h && sp - p == number->m_len) {
				if (number->m_number == 100) {
					if (hn) break;
					if (!sm) { sm = s_smallNumbers + 1; }
					hn = sm;
					sm = NULL;
				} else {
					if (tn) break;
					tn = number;
				}
				match = true;
				break;
			}
		}
		if (!sm && !match) for (Number *number = s_smallNumbers;
			number->m_word; number++) {
			if (h == number->m_h && sp - p == number->m_len) {
				match = true;
				sm = number;
				break;
			}
		}
		if (match) {
			if (md) {
				if (hn) m += 100 * hn->m_number;
				if (tn) m += tn->m_number;
				if (sm) m += sm->m_number;
				m *= md->m_number;
				n += m;
				m = 0;
				sm = tn = hn = md = NULL;
			}
		} else if (p < pend) {
			//log(LOG_DEBUG, "query: mismatch %s %lld", p, n);
			return false;
		}
		p = sp;
	}
	if (hn) n += 100 * hn->m_number;
	if (tn) n += tn->m_number;
	if (sm) n += sm->m_number;
	if (buf.safePrintf("%lld", n)) {
		return info->addSynonym(buf.getBufStart(), -1, -1, 
				 buf.length(), SYN_NUMBER, 2, false, 0, 0);
	}
	return false;
}

static char *s_articlesEng[] = { "the",
				 "an",
				 "a"};

// MDW: "some is not a stop word and should be ommitted
//				 "some"};

static int32_t s_numArticlesEng = 3;

bool Thesaurus::generatePhrases(char *s, int32_t slen, 
				SynonymInfo *info, int32_t bits) {
	char *w1, *w2, *p1, *p2, *end, *mid = NULL;
	int32_t w1Len, w2Len, midLen;
	int64_t leftSynHash, rightSynHash;
	// disable this lest we get into an infinite recursive loop
	bits &= ~SYNBIT_PHRASE;
	p1 = s;
	// find first non-stopword
	bool isStop;
	end = s + slen;
	char **articles = s_articlesEng;
	int32_t numArticles = s_numArticlesEng;
	do {
		while (*p1 &&  isspace(*p1) && p1 < end) p1++;
		w1 = p1;
		while (*p1 && !isspace(*p1) && p1 < end) p1++;
		w1Len = p1 - w1;
		isStop = isStopWord(w1, w1Len, hash64Lower_utf8(w1, w1Len));
	} while (p1 < end && *p1 && isStop);
	// we reached the end without finding a non-stopword
	//  probably shouldn't have gotten fed this
	if (!w1 || p1 == w1 || !*p1 || isStop) {
		// not sure if this should be a logic error?
		log(LOG_DEBUG, "query: non-phrase fed to generatePhrases");
		return false;
	}
	// find second non-stopword
	do {
		while (*p1 &&  isspace(*p1) && p1 < end) p1++;
		w2 = p1;
		if (!mid) mid = p1;
		while (*p1 && !isspace(*p1) && p1 < end) p1++;
		w2Len = p1 - w2;
		isStop = isStopWord(w2, w2Len, hash64Lower_utf8(w2, w2Len));
	} while (p1 < end && *p1 && isStop);
	// carve out the middle for later
	midLen = w2 - mid;
	while (isspace(mid[midLen - 1])) midLen--;
	// we reached the end without finding a second non-stopword
	// this happens with certain phrase segments, e.g. "cheese and"
	if (!w2 || p1 == w2 || isStop) return false;
	SynonymInfo syn1, syn2;
	bool r  = getAllInfo(w1, &syn1, w1Len, bits);
	     r |= getAllInfo(w2, &syn2, w2Len, bits);
	if (!r) return false;
	// check to see if there is an article for the first stop word
	p1 = mid;
	char *stop;
	int32_t stopLen;
	while(*p1 &&  isspace(*p1) && p1 < w2) p1++;
	stop = p1;
	while(*p1 && !isspace(*p1) && p1 < w2) p1++;
	stopLen = p1 - stop;
	int32_t artIndex = -1;
	if (stopLen > 0) {
		artIndex = numArticles - 1;
		while (artIndex >= 0) {
			if (!strncmp(stop, articles[artIndex], stopLen)) {
				break;
			}
			artIndex--;
		}
	}	
	r = false;
	// -1 is for the original source
	// i is for the first part, j is for the second part, and k is for
	//  arcticle substitution
	for (int i = -1; i < syn1.m_numSyns; i++) { 
		for (int j = -1; j < syn2.m_numSyns; j++) {
			for (int k = -1; k < numArticles; k++) {
				if (artIndex < 0) k = numArticles;
				if ((i < 0 && j < 0) && 
				    (artIndex < 0 || artIndex == k)) 
					continue;
				// check for 'an' and only use if w2 starts
				//  with a vowel, and don't use 'a' if w2
				//  starts with a vowel
				char vw;
				if (j < 0) vw = w2[0];
				else       vw = syn2.m_syn[j][0];
				if ((k == 1 && !isvowel(vw)) ||
				    (k == 2 &&  isvowel(vw))) 
					continue;
				char buf[2048];
				char sort = 0;
				p1 = s;
				p2 = buf;
				int32_t n1, n2;
				// copy the fragment before w1
				n1 = w1 - p1;
				n2 = n1;
				gbmemcpy(p2, p1, n2);
				p1 += n1;
				p2 += n2;
				// copy the w1 synonym
				n1 = w1Len;
				if (i < 0) {
					n2 = n1;
					gbmemcpy(p2, w1, n1);
					leftSynHash = 0;
				} else {
					n2 = syn1.m_len[i];
					gbmemcpy(p2, syn1.m_syn[i], n2);
					//lid = syn1.m_termId[i];
					leftSynHash = syn1.m_synHash[i];
					sort += syn1.m_sort[i];
				}
				p1 += n1;
				p2 += n2;
				if (k < numArticles) {
					n1 = stopLen;
					if (k == -1) {
						n2 = 0;
					} else {
						*p2++ = ' ';
						n2 = gbstrlen(articles[k]);
						gbmemcpy(p2, articles[k], n2);
					}
					p1 += n1 + 2;
					p2 += n2;
					*p2++ = ' ';
					if (midLen > stopLen) {
						n1 = midLen - stopLen;
						n2 = n1;
						gbmemcpy(p2, p1, n2);
						p1 += n1 + 1;
						*p2++ = ' ';
					}
				} else {
					// copy the fragment between w1 and 2
					n1 = w2 - (w1 + w1Len);
					n2 = n1;
					gbmemcpy(p2, p1, n2);
					p1 += n1;
					p2 += n2;
				}
				// copy the w2 synonym
				n1 = w2Len;
				if (j < 0) {
					n2 = n1;
					gbmemcpy(p2, w2, n1);
					rightSynHash = 0;
				} else {
					n2 = syn2.m_len[j];
					gbmemcpy(p2, syn2.m_syn[j], n2);
					//rid = syn2.m_termId[j];
					rightSynHash = syn2.m_synHash[j];
					sort += syn2.m_sort[j];
				}
				p1 += n1;
				p2 += n2;
				// copy the fragment after w2
				n1 = (s + slen) - (w2 + w2Len);
				n2 = n1;
				gbmemcpy(p2, p1, n2);
				p1 += n1;
				p2 += n2;
				*p2 = '\0';
				r |= info->addSynonym(buf, -1, -1, 
						      p2 - buf, SYN_PHRASE, 
						      sort, true, 
						      leftSynHash, 
						      rightSynHash);
			}
		}
	}
	return r;
}

struct synType {
	char *m_word;
	char m_type;
}; 

static synType s_types[] = {
		{ "synonym", SYN_SYNONYM },
		{ "stem", SYN_STEM },
		{ "spelling", SYN_SPELLING },
		{ "acronym", SYN_ACRONYM },
		{ "number", SYN_NUMBER },
		{ "phrase", SYN_PHRASE },
		{ "translation", SYN_TRANS }, 
		{ "unknown", SYN_UNKNOWN },
		{ "invalid", SYN_INVALID }
	};

bool Thesaurus::rebuildSynonyms() {
	uint64_t startTime = gettimeofdayInMilliseconds();
	char ff[PATH_MAX];
	char *p1, *pend;
	HashTableT<uint64_t, SynonymLinkGroup> linkTable;

	// read in all files that fit the pattern
	Dir dir;
	dir.set(g_hostdb.m_dir, s_dictDir);
	if (!dir.open())
		return log("build: Couldn't open directory %s%s",
			g_hostdb.m_dir, s_dictDir);

	char *synFile;

	SafeBuf addBuffer;
	int32_t unknown = 0;	// number of missing synonym types

	while((synFile = dir.getNextFilename("thesaurus-*"))) {
		// don't read this, the format is different
		if (!strcmp(synFile, s_affFile)) continue;
		snprintf(ff, PATH_MAX, "%s%s%s", 
			g_hostdb.m_dir, s_dictDir, synFile);
		SafeBuf addFile;

		if (!addFile.fillFromFile(ff)) {
			log("build: Couldn't load %s", ff);
			continue;
		}

		log(LOG_INFO, "build: Reading synonym pairs from %s", ff);

		p1 = addFile.getBufStart();
		pend = addFile.getBuf();
		// one word/phrase pair per line, read first as master, second 
		//  as synonym, delimited by a pipe '|' character to support 
		//  phrase synonyms
		while(p1 < pend) {
			char *a = p1, *b = NULL, *c = NULL, 
			     *d = NULL, *e = NULL;
			while (*p1 != '\n' && p1 < pend) p1++;
			if (*p1 == '\n') *p1++ = '\0';
			b = strchr(a, '|');
			if (b) c = strchr(b + 1, '|');
			if (c) d = strchr(c + 1, '|');
			if (d) e = strchr(d + 1, '|');
			if (!b || e) {
				log(LOG_DEBUG, "build: Line in %s doesn't "
					"contain the right number of pipes: "
					"\"%s\", skipping line", ff, a);
				continue;	
			} else {
				*b++ = '\0';
				if (c) *c++ = '\0';
				if (d) *d++ = '\0';
			}
			SynonymLinkGroup w, *wp1, *wp2;
			uint64_t h1, h2;
			int32_t alen = gbstrlen(a), blen = gbstrlen(b);
			char type = SYN_UNKNOWN;
			int32_t aff = -1;
			// if we have both but the third field is a number,
			//  assign it to d for affinity
			if (c && d && isdigit(*c)) {
				char *s = c;
				c = d;
				d = s;
			}
			if (c) {
				for (synType *typep = s_types; 
					typep->m_type != SYN_UNKNOWN;
					typep++) {
					if (!strcmp(c, typep->m_word)) {
						type = typep->m_type;
						break;
					}
				}
			}
			if (d) aff = strtol(d, &e, 0);
			if (type >= SYN_UNKNOWN) {
				if (c) {
					log(LOG_DEBUG, "build: Unknown synonym "
						"type: %s", c);
				} else {
					log(LOG_DEBUG, "build: Missing synonym "
						"type: %s, %s", a, b);
				}
				unknown++;
			}
			if (e && *e) log("build: Extra characters in "
				"affinity: %s", e);
			h1 = hash64Lower_utf8(a, alen);
			h2 = hash64Lower_utf8(b, blen);
			int32_t slot1 = linkTable.getSlot(h1);
			bool x = true;
			if (slot1 < 0) {
				w.m_n = 0;
				w.m_h[0] = h1;
				w.m_syn[0] = addBuffer.length();
				addBuffer.safeMemcpy(a, alen+1);
				x &= linkTable.addKey(h1, w, &slot1);
			}
			int32_t slot2 = linkTable.getSlot(h2);
			if (slot2 < 0) {
				w.m_n = 0;
				w.m_h[0] = h2;
				w.m_syn[0] = addBuffer.length();
				addBuffer.safeMemcpy(b, blen+1);
				x &= linkTable.addKey(h2, w, &slot2);
				// this slot may have moved so grab it again
				slot1 = linkTable.getSlot(h1);
			}
			if (!x) {	// ran out of memory
				return log("build: Out of memory rebuilding "
					"synonym list, aborting.");
			}
			wp1 = linkTable.getValuePointerFromSlot(slot1);
			wp2 = linkTable.getValuePointerFromSlot(slot2);
			int i, j;
			// make sure they aren't already in the lists
			for (i = 0; i < wp1->m_n; i++)
				if (h2 == wp1->m_h[i+1]) break;
			for (j = 0; j < wp2->m_n; j++)
				if (h1 == wp2->m_h[j+1]) break;
	
			if (i == wp1->m_n) { // couldn't find it, so add it
				if (i >= MAX_SYNS - 1) {
					log("build: Too many links in "
						"thesaurus for %s, not adding "
						"%s", a, b);
				} else {
					i++;
					wp1->m_n++;
					wp1->m_h[i] = h2;
					wp1->m_syn[i] = wp2->m_syn[0];
					wp1->m_type[i] = type;
					wp1->m_aff[i] = aff;
				}
			} else if (aff >= 0) {	// found it and we override
				wp1->m_aff[i] = aff; 
			}
			if (j == wp2->m_n) { // couldn't find it, so add it
				if (j >= MAX_SYNS - 1) {
					log("build: Too many links in "
						"thesaurus for %s, not adding "
						"%s", b, a);
				} else {
					j++;
					wp2->m_n++;
					wp2->m_h[j] = h1;
					wp2->m_syn[j] = wp1->m_syn[0];
					wp2->m_type[j] = type;
					wp2->m_aff[j] = -1;
				}
			}
		}
	}

	// make sure it's null-terminated in case of bad formatting in the
	//  add files
	if (*(addBuffer.getBuf()-1) != '\0')
		addBuffer.pushChar('\0');

	int32_t numSynonyms = 0;
	int32_t totalPairs = 0;
	// count up groups that have at least 2 members
	for (int32_t slot = 0; slot < linkTable.getNumSlots(); slot++) {
		SynonymLinkGroup w;
		w = linkTable.getValueFromSlot(slot);
		if (w.m_n) numSynonyms++;
		totalPairs += w.m_n;
	}

	log(LOG_INFO, "build: Built %"INT32" synonym groups and %"INT32" pairs", 
		numSynonyms, totalPairs);
	if (unknown) log(LOG_WARN, "build: %"INT32" synonyms pairs were missing "
		"valid types, check your input files", unknown);

	SafeBuf thesFile;

	thesFile.safePrintf("|lastRebuild|%"INT32"\n", m_lastRebuild);
	thesFile.safePrintf("|numSynonyms|%"INT32"\n", numSynonyms);
	thesFile.safePrintf("|totalPairs|%"INT32"\n", totalPairs);
	thesFile.safePrintf("|totalSlots|0\n");

	for (int32_t slot = 0; slot < linkTable.getNumSlots(); slot++) {
		if (!linkTable.getKey(slot)) continue;
		SynonymLinkGroup w;
		w = linkTable.getValueFromSlot(slot);
		// this won't run if w.m_n = 0
		for (int j = 1; j <= w.m_n; j++) {
			char *s1, *s2;
			if (w.m_h[0] == w.m_h[j]) continue;
			s1 = addBuffer.getBufStart() + w.m_syn[0];
			s2 = addBuffer.getBufStart() + w.m_syn[j];
			int32_t aff;
			if (w.m_aff[j] >= 0) aff = w.m_aff[j];
			else                 aff = getAffinity(s1, s2);
			if (aff >= 0) {
				thesFile.safePrintf("%s|%s|0x%08"XINT32"|%"INT32"\n", 
					s1, s2, aff, (int32_t)w.m_type[j]);
			} else {
				thesFile.safePrintf("%s|%s|%"INT32"|%"INT32"\n",
					s1, s2, aff, (int32_t)w.m_type[j]);
			}
		}
	}

	snprintf(ff, PATH_MAX, "%s%sthesaurus.txt", g_hostdb.m_dir, s_dictDir);
	if (!thesFile.dumpToFile(ff)) return log("build: Couldn't save %s", ff);

	log(LOG_TIMING, "build: took %"INT64"ms to rebuild synonyms",
		gettimeofdayInMilliseconds() - startTime);
	return true;
}

class StateAffinityGroup {
public:
	StateAffinityGroup();
	StateAffinity *m_aff;
	SynonymInfo m_info;
	char *m_syn;
	int32_t m_i;
	int32_t m_sent;
	int32_t m_cache;
	int32_t m_recv;
	bool m_next;
};

StateAffinityGroup::StateAffinityGroup() {
	m_aff = NULL;
	m_syn = NULL;
	m_i = 0;
	m_sent = 0;
	m_cache = 0;
	m_recv = 0;
	m_next = false;
}


static void buildAffinity(StateAffinity *aff);
static StateAffinityGroup *getNextAffinityGroup(StateAffinity *aff);
static StateAffinityGroup *buildAffinityGroup(StateAffinityGroup *group);
static void gotAffinityDoc(void *state, TcpSocket *socket);
static void affinityRetry(StateAffinityGroup *group, TcpSocket *socket);
static void affinityAbort(StateAffinityGroup *group);
static void gotAffinityIP(void *state, int32_t ip);
static void gotAllAffinityPairs(void *state);
static void gotGroupAffinityPairs(StateAffinityGroup *group);

// start the callback loop, this basically stops running after we send out 
//  enough requests
static void buildAffinity(StateAffinity *aff) {
	StateAffinityGroup *group = getNextAffinityGroup(aff);
	do {
		if (aff->m_n >= aff->m_next) {
			log(LOG_INFO, "build: %"INT32" out of %"INT32" pairs built",
				aff->m_n, aff->m_oldTable->getNumSlotsUsed());
			aff->m_next = aff->m_n + 1000;
			QUICKPOLL(1);	// just in case we're hogging the cpu 
					//  with lots of already built pairs
		}
	} while((group = buildAffinityGroup(group)));
}

static StateAffinityGroup *getNextAffinityGroup(StateAffinity *aff) {
	StateAffinityGroup *group;
	if (!aff->m_thes->m_rebuilding) return NULL;
	if (aff->m_syn == aff->m_synend) return NULL;
	try { group = new(StateAffinityGroup); }
	catch( ... ) {
		log("build: Couldn't allocate %i bytes for thesaurus, "
			"aborting rebuild", sizeof(StateAffinityGroup));
		aff->m_thes->m_rebuilding = false;
		return NULL;
	}
	mnew(group, sizeof(StateAffinityGroup), "thesaurus");
	group->m_aff = aff;
	group->m_syn = aff->m_syn;
	aff->m_thes->getSynonymInfo(group->m_syn, &group->m_info);
	group->m_i = group->m_info.m_numSyns - 1;
	aff->m_n += group->m_i + 2;
	aff->m_syn += gbstrlen(aff->m_syn) + 1;
	return group;
}

// returns a pointer to the group to process (sometimes ourselves, sometimes
//  the next group), or NULL if we need to stop
static StateAffinityGroup *buildAffinityGroup(StateAffinityGroup *group) {
	StateAffinity *aff = group->m_aff;
	SynonymInfo *info = &group->m_info;
	// too many requests going at once, return without doing anything
	// the callback will call us again when a request is finished
	if (aff->m_sent >= aff->m_recv + g_conf.m_maxAffinityRequests)
		return NULL;
	// we're done sending out requests for this chain or we aborted
	if (group->m_i < -1 || !aff->m_thes->m_rebuilding) {
		StateAffinityGroup *group2 = NULL;
		// grab the next group if need be 
		if (!group->m_next) {
			// this will be NULL if we aborted or we're done
			group2 = getNextAffinityGroup(aff);
			group->m_next = true;
		}
		// we sent all our requests AND we have all our responses
		if (group->m_recv == group->m_sent)
			gotGroupAffinityPairs(group);
		// call the final callback if EVERYTHING is back and we have
		//  nothing else to send
		if (aff->m_recv == aff->m_sent && !group2) 
			aff->m_callback(aff);
		return group2;
	}
	int32_t i = group->m_i;
	uint64_t hh;
	char *s1 = group->m_syn;
	char *s2 = "";
	int32_t  h1 = hash32n(group->m_syn);
	int32_t  h2;
	SafeBuf b;
	b.safePrintf("http://%s/search?q=", aff->m_server);
	if (strchr(s1, ' ')) {
		b.urlEncode("\"", 1);
		b.urlEncode(s1, gbstrlen(s1));
		b.urlEncode("\"", 1);
	} else {
		b.urlEncode(s1, gbstrlen(s1));
	}
	if (i == -1) {
		hh = h1;
	} else {
		s2 = info->m_syn[i];
		h2 = hash32n(s2);
		b.urlEncode(" .. ", 4);
		if (strchr(s2, ' ')) {
			b.urlEncode("\"", 1);
			b.urlEncode(s2, gbstrlen(s2));
			b.urlEncode("\"", 1);
		} else {
			b.urlEncode(s2, gbstrlen(s2));
		}
		// hits for (i,j) is the same as (j,i), but the hash will be
		//  different so we need to order it properly
		if (strcmp(s1, s2) < 0) {
			hh = h1 + ((uint64_t)h2 << 32);
		} else {
			hh = h2 + ((uint64_t)h1 << 32);
		}
	}
	b.safeMemcpy(g_conf.m_affinityParms, gbstrlen(g_conf.m_affinityParms)+1);
	uint64_t *llp;
	if (!aff->m_fullRebuild && i >= 0 && info->m_affinity[i] >= 0) {
		log(LOG_DEBUG, "build: old value: (%s, %s, %08"XINT32")",
			s1, s2, info->m_affinity[i]);
		aff->m_old++;
	} else if (i == -1 && !group->m_sent && !group->m_cache) {
		// we used nothing but old values for this group so we don't
		//  need to send this either
		log(LOG_DEBUG, "build: old value: (%s)", s1);
		aff->m_old++;
	} else if (!(llp = aff->m_hitsTable.getValuePointer(hh))) {
		//Url url;
		//url.set(b.getBufStart(), b.length(), aff->m_ip, 0, 0, 0, 0);
		if (!g_httpServer.getDoc( b.getBufStart(),//&url,
					  0,//ip
					  0, -1, 0,
					  group, gotAffinityDoc,
					  g_conf.m_affinityTimeout, 0, 0,
					  32768, 32768,
					  0)) { 
			group->m_sent++;
			aff->m_sent++;
		} else {
			log("build: getDoc error: %s (%s)", mstrerror(g_errno), 
				b.getBufStart());
			affinityAbort(group);
			// let it fall through so it cleans itself up
		}
	} else {
		if (i >= 0) {
			log(LOG_DEBUG, "build: cache hit (%s, %s, %"INT64")", 
				s1, s2, *llp);
		} else {
			log(LOG_DEBUG, "build: cache hit (%s, %"INT64")", 
				s1, *llp);
		}
		aff->m_cache++;
		group->m_cache++;
	}
	group->m_i--;
	// return ourselves so we run again
	return group;
}

static void gotAffinityDoc(void *state, TcpSocket *socket) {
	StateAffinityGroup *group = (StateAffinityGroup *)state;
	StateAffinity *aff = group->m_aff;
	SynonymInfo *info = &group->m_info;
	int32_t i = -1;
	char *q = strstr(socket->m_sendBuf, "q=") + 2;
	Xml xml;
	group->m_recv++;
	aff->m_recv++;
	// the stuff below might no longer be valid (synonyms specifically)
	if (!aff->m_thes->m_rebuilding) {
		// do cleanup
		buildAffinityGroup(group);
		return;
	}
	char *qend = strchr(q, '&');
	char buf[1024];
	int32_t qlen = urlDecode(buf, q, qend - q);
	buf[qlen] = '\0';
	char *sep = buf + gbstrlen(group->m_syn);
	if (buf[0] == '\"') sep += 2;
	if (!strncmp(sep, " .. ", 4)) { // are we a pair?
		char *syn = sep + 4;    // step over the " .. " in the middle
		for (int32_t j = 0; j < info->m_numSyns; j++) {
			uint32_t h1 = hash32n(info->m_syn[j]);
			uint32_t h2;
			int32_t slen = buf - syn + qlen;
			if (syn[0] == '\"') {
				h2 = hash32(syn + 1, slen - 2);
			} else {
				h2 = hash32(syn, slen);
			}
			if (h1 == h2) {
				i = j;
				break; 
			}
		}
		if (i == -1) {
			log("build: i == -1 but we have a pair");
			char *xx = NULL; *xx = 0;
		}
	}
	if (g_errno) {
		log("build: affinity socket error: %s", mstrerror(g_errno));
		affinityRetry(group, socket);
		return;
	}
	if (!socket->m_totalToRead) {
		log("build: affinity socket error, no data to read");
		affinityRetry(group, socket);
		return;
	}	
	if (socket->m_totalRead != socket->m_totalToRead) {
		log("build: affinity socket error, read did not complete");
		affinityRetry(group, socket);
		return;
	}
	char *s;
	s = strstr(socket->m_readBuf, "\r\n\r\n");
	if (!s || (s - socket->m_readBuf) > socket->m_readOffset) {
		log("build: invalid HTTP response during affinity rebuild");	
		affinityRetry(group, socket);
		return;
	}
	s += 4;
	int32_t len;
	len = socket->m_readOffset - (s - socket->m_readBuf);
	if (strncmp(s, "<?xml", 5)) {
		log("build: Non-XML response during affinity rebuild");
		log("build: s = %32s", s);
		affinityRetry(group, socket);
		return;
	} 
	if (!xml.set(s, len, false, 0, false, 0)) {
		log("build: len = %"INT32"", len);
		log("build: s = %32s", s);
		affinityRetry(group, socket);
		return;
	}
	int64_t hits;
	hits = xml.getLongLong("Report.hits", -1LL);
	if (hits == -1LL) {
		log("build: hits tag not found in XML response");
		log("build: s = %256s", s);
		affinityRetry(group, socket);
		return;
	}
	uint64_t hh;
	uint32_t h1 = hash32n(group->m_syn);
	if (i == -1) {
		hh = (uint64_t)h1;
	} else {
		char *s1 = group->m_syn;
		char *s2 = info->m_syn[i];
		uint32_t h2 = hash32n(s2);
		if (strcmp(s1, s2) < 0) {
			hh = h1 + ((uint64_t)h2 << 32);
		} else {
			hh = h2 + ((uint64_t)h1 << 32);
		}
	}
	if (!aff->m_hitsTable.addKey(hh, hits)) {
		log("build: Ran out of memory while rebuilding affinity, "
			"aborting.");
		affinityAbort(group);
		return;
	} 

	// send the next request and/or do cleanup
	do {
		if (aff->m_n >= aff->m_next) {
			log(LOG_INFO, "build: %"INT32" out of %"INT32" pairs built",
				aff->m_n, aff->m_oldTable->getNumSlotsUsed());
			aff->m_next = aff->m_n + 1000;
			QUICKPOLL(1);	// just in case we're hogging the cpu 
					//  with lots of already built pairs
		}
	} while((group = buildAffinityGroup(group)));
}

static void affinityRetry(StateAffinityGroup *group, TcpSocket *socket) {
	StateAffinity *aff = group->m_aff;
	if (!aff->m_thes->m_rebuilding) return;
	if (aff->m_errors >= g_conf.m_maxAffinityErrors) {
		if (aff->m_thes->m_rebuilding)
			log("build: exceeded affinity retry limit, aborting");
		affinityAbort(group);
		return;
	}
	aff->m_errors++;
	log(LOG_DEBUG, "build: affinity error #%"INT32"", aff->m_errors);
	// rebuild the url from the sendBuf
	char buf[1024], *p; 
	p = buf; 
	strcpy(p, "http://"); p += 7;
	// first, the host
	char *p2, *p2end;
	p2 = strstr(socket->m_sendBuf, "Host: ") + 6;
	p2end = strstr(p2, "\r\n");
	strncpy(p, p2, p2end - p2); p += p2end - p2;
	// then the port
	p += sprintf(p, ":%i", socket->m_port);
	// then the rest of the url
	p2 = socket->m_sendBuf + 4; 
	p2end = strstr(socket->m_sendBuf, " HTTP");
	strncpy(p, p2, p2end - p2); p += p2end - p2;
	*p = '\0';
	//Url url;
	//url.set(buf, gbstrlen(buf), aff->m_ip, 0, 0, 0, 0);
	// resend the request
	if (!g_httpServer.getDoc(buf,//&url,
				 0,//ip
				  0, -1, 0,
				  group, gotAffinityDoc,
				  30000, 0, 0,
				  32768, 32768,
				  0)) {
		group->m_sent++;
		aff->m_sent++; 
		return;
	}
	log("build: getDoc error: %s (%s)", mstrerror(g_errno), buf);
	affinityAbort(group);
}

static void affinityAbort(StateAffinityGroup *group) {
	group->m_aff->m_thes->m_rebuilding = false;
	// do cleanup
	buildAffinityGroup(group);
}

static void gotAffinityIP(void *state, int32_t ip) {
	StateAffinity *aff = (StateAffinity *)state;
	if (!ip) {
		log("build: Couldn't resolve %s for affinity rebuild",
			aff->m_server);
		aff->m_thes->m_rebuilding = false;
		aff->m_thes->m_affinityState = NULL;
		delete(aff);
		mdelete(aff, sizeof(StateAffinity), "thesaurus");
		return;
	}
	aff->m_ip = ip;
	// start the loop
	buildAffinity(aff);
}

static void gotGroupAffinityPairs(StateAffinityGroup *group) {
	StateAffinity *aff = group->m_aff;
	SynonymInfo *info = &group->m_info;
	char *s1 = group->m_syn;
	//int32_t s1len = gbstrlen(s1);
	uint64_t key = hash64Lower_utf8(s1);
	 int64_t v = aff->m_thes->getValueN(key, 0);
	int32_t numSyns = info->m_numSyns;
	log(LOG_DEBUG, "build: gotGroupAffinityPairs(%p)", group);
	aff->m_newTable->addKey(key, v);
	for(int32_t i = 0; i < numSyns; i++) { 
		if (!aff->m_thes->m_rebuilding) continue;
		char *s2 = info->m_syn[i];
		//int32_t s2len = gbstrlen(s2);
		uint64_t hh;
		uint32_t hh1 = hash32n(s1), hh2 = hash32n(s2);
		if (info->m_affinity[i] >= 0 &&	!aff->m_fullRebuild) {
			// if we're not doing a full rebuild, use the old value
			//  if it exists and is valid
			// these values never have bit 31 set
			v = ((int64_t)info->m_affinity[i] << 32) + 
				((int64_t)info->m_type[i] << 27) +
				info->m_offset[i];
			aff->m_newTable->addKey(key, v);
			continue;
		}
		if (strcmp(s1, s2) > 0) {
			hh = hh2 + ((uint64_t)hh1 << 32);
		} else {
			hh = hh1 + ((uint64_t)hh2 << 32);
		}
		uint64_t k = 0, l = 0, *pk, *pl;
		pk = aff->m_hitsTable.getValuePointer((uint64_t)hh1);
		pl = aff->m_hitsTable.getValuePointer(hh);
		if (pk) k = *pk;
		if (pl) l = *pl;
		int32_t a = -1;
		if (k && l) {
			double f = (double)l / (double)k;
			// we never want synonym affinity to be 100%
			if (f > 0.99) f = 0.99;
			a = (int32_t)(f * MAX_AFFINITY);
		} else if (pk && pl) {
			a = 0;
		} else {
			log(LOG_WARN, "build: hits=%s,%08"XINT32",%p,%"INT64","
				"%s,%016"XINT64",%p,%"INT64"", 
				s1, (uint32_t) hh1, pk, k, 
				s2, hh, pl, l);
			continue;
		}
		log(LOG_DEBUG, "build: affinity(%s,%s)=%"INT32"(%"INT64",%"INT64")",
			s1, s2, a, k, l);
//		if (a < MAX_AFFINITY * 0.01) {
//			aff->m_skip++;
//			continue;
//		}
		v = ((int64_t)a << 32) + ((int64_t)info->m_type[i] << 27) + 
			info->m_offset[i];
		aff->m_newTable->addKey(key, v);
		aff->m_built++;
	}
	delete group;
	mdelete(group, sizeof(StateAffinityGroup), "thesaurus");
}

// for logic warnings
/*static uint64_t s_cmpKey;

static int slotCmp(const void *p1, const void *p2) {
	int64_t v1 = *(const int64_t *)p1, 
	        v2 = *(const int64_t *)p2;
	// check for invalid affinity (negative) and push those back
	// else sort by the affinity (stored in the high 32 bits) and then
	//  by offset into the text buffer if there's a tie for some reason
	if      (v2 <  0) return -1;	// v2 is invalid, push it back
	else if (v1 <  0) return  1;	// v1 is invalid, push it back
	else if (v1 > v2) return -1;	// v1 has higher affinity, push it up
	else if (v1 < v2) return  1;	// v2 has higher affinity, push it up
	else {
		// if this happens the code elsewhere is borked
		log(LOG_LOGIC, "build: duplicate entry (%016"XINT64",%016"XINT64")",
			s_cmpKey, v1);
		return  0;
	}
} */

/*
static void sortTable(HashTableT<uint64_t, int64_t> *table) {
	int32_t n1 = table->getNumSlots();
	// this is a bit ugly but it's the best way to get the synonyms sorted
	//  as far as I can figure out
	
	for (int32_t i = 0; i < n1; i++) {
		uint64_t key = table->getKey(i);
		// check for an empty slot or if we're at the first slot for 
		//  this key, if we're not we already sorted this key set
		if (!key || (table->getSlot(key) != i)) continue;
		// if not, count up all the slots that use this key and store
		//  them in a temporary array
		int32_t n2 = 0, j = i;
		int32_t slots[MAX_SYNS];
		int64_t vals[MAX_SYNS];
		while(table->getKey(j)) {
			if (table->getKey(j) == key) {
				slots[n2] = j;
				vals[n2] = table->getValueFromSlot(j);
				n2++;
			}
			if (++j >= table->getNumSlots()) j = 0;
		}
		// and then sort them
		s_cmpKey = key;
		gbmergesort(vals, n2, sizeof(int64_t), slotCmp);
		// and then throw them back in the table
		for (int32_t j = 0; j < n2; j++) {
			table->setValue(slots[j], vals[j]);
		} 
	}
}
*/

static void gotAllAffinityPairs(void *state) { 
	StateAffinity *aff = (StateAffinity *)state;
	log(LOG_DEBUG, "build: gotAllAffinityPairs(%p)", state);
	if (aff->m_thes->m_rebuilding) {
		log(LOG_INFO, "build: Rebuilt %"INT32" affinity pairs, sent "
			"%"INT32" total requests, hit cache %"INT32" times, used %"INT32" "
//			"old values, had %"INT32" errors, dropped %"INT32" pairs for "
//			"values below the threshold, and took %"INT64" seconds"
			"old values, had %"INT32" errors, and took %"INT64" seconds"
			"(%s rebuild)", 
			aff->m_built, aff->m_sent, aff->m_cache,
			aff->m_old, aff->m_errors, //aff->m_skip,
			(gettimeofdayInMilliseconds() - aff->m_time) / 1000,
			aff->m_fullRebuild ? "full" : "partial");
		// do the overrides now, before we copy the table over
		char ff[PATH_MAX];
		SafeBuf addFile;
		snprintf(ff, PATH_MAX, "%s%s%s", 
			g_hostdb.m_dir, s_dictDir, s_affFile);
		addFile.fillFromFile(ff);
		char *pstart = addFile.getBufStart();
		char *p = pstart;
		char *pend = addFile.getBuf();
		// format is "%s|%s|%f/d", word pair (a, b), and either a float
		//  or 32 bit hex integer designating the affinity, floating
		//  point is probably more 'portable' in case MAX_AFFINITY ever
		//  changes, also supports the word 'max' (case sensitive) to
		//  designate maximum affinity
		// pipe-delimited triplet per line
		while (p < pend) {
			char *a = p;
			while (*p != '\n' && p < pend) p++;
			if (*p == '\n') *p++ = '\0';

			// verify that there are two pipes per line
			char *b = NULL, *c = NULL, *e = NULL;
			b = strchr(a, '|');
			if (b) c = strchr(b + 1, '|');
			if (c) e = strchr(c + 1, '|');
			if (!b || !c || e) {
				log(LOG_DEBUG, "build: Bad format in %s, line "
					"does not contain exactly two pipes: "
					"\"%s\", skipping line", ff, p);
				break;
			} else {
				*b++ = '\0';
				*c++ = '\0';
			}
			int32_t val;
			char *d = NULL;
			if (strcmp(c, "max") == 0) {
				val = MAX_AFFINITY;
			} else if (strchr(c, '.')) { // floating point
				float f = strtod(c, &d);
				if (f > 0.99) f = 0.99;
				if (f < 0.0 ) f = 0.0;
				val = (int32_t)(f * MAX_AFFINITY);
			} else {
				val = strtol(c, &d, 0);
			}
			if (d && *d) log(LOG_DEBUG, "build: Extra characters "
				"in affinity value: %s", d);
			uint64_t h = hash64Lower_utf8(a);
			int32_t slot = aff->m_newTable->getSlot(h);
			int32_t offset = aff->m_thes->getOffset(b);
			if (slot < 0) {
				log("build: Couldn't find synonym slot for "
					"(%s)", a);
				continue;
			}
			if (offset < 0) {
				log("build: Couldn't find synonym slot for "
					"(%s)", b);
				continue;
			}
			uint64_t k;
			int64_t v;
			do {
				if (++slot >= aff->m_newTable->getNumSlots())
					slot = 0;
				k = aff->m_newTable->getKey(slot);
				v = aff->m_newTable->getValueFromSlot(slot);
			} while (k && (k != h || OFFSET(v) != offset));
			if (!k) {
				log("build: Couldn't find synonym slot for "
					"(%s,%s)", a, b);
			} else {
				int64_t nv = ((int64_t)val << 32) + 
					(v & 0xFFFFFFFF);
				aff->m_newTable->setValue(slot, nv);
			}
		}
		if (aff->m_fullRebuild || !aff->m_old) {
			aff->m_thes->m_lastRebuild = 
				gettimeofdayInMilliseconds() / 1000;
			log(LOG_INFO, "build: Affinity timestamp updated");
		}
		//sortTable(aff->m_newTable);
		aff->m_oldTable->copy(aff->m_newTable);
		if (aff->m_thes->save()) {
			log(LOG_INFO, "build: propogating thesaurus data to "
				"all hosts");
			char cmd[512];
			for ( int32_t i = 0; i < g_hostdb.getNumHosts() ; i++ ) {
				Host *h = g_hostdb.getHost(i);
				snprintf(cmd, 512,
					"rcp -r "
					"%s%sthesaurus* "
					"%s:%s%s &",
					g_hostdb.m_dir,
					s_dictDir,
					iptoa(h->m_ip),
					h->m_dir,
					s_dictDir);
				log(LOG_INFO, "admin: %s", cmd);
				system( cmd );
			}
		} else {
			log("build: Couldn't save thesaurus data: (%s), "
				"will try again later", mstrerror(g_errno));
		}
	} else {
		log(LOG_INFO, "build: Affinity rebuild aborted, table "
			"unchanged");
	}
	delete(aff);
	mdelete(aff, sizeof(StateAffinity), "thesaurus");
	aff->m_thes->m_rebuilding = false;
	g_thesaurus.m_affinityState = NULL;
}

bool Thesaurus::rebuildAffinity(char *server, bool fullRebuild) {
	char *syn = m_synonymText, *synend = syn + m_synonymLen;
	if (g_conf.m_maxAffinityAge >= 0 && 
		((gettimeofdayInMilliseconds() / 1000 - m_lastRebuild) 
		 / 86400 > g_conf.m_maxAffinityAge)
		) {
		fullRebuild = true;
	}
	if (m_rebuilding) {
		log("build: Ignoring rebuild request while already rebuilding");
		return true;
	}
	if (m_affinityState) {
		log("build: Still cleaning up affinity from abort, not "
			"restarting");
		return true;
	}
	if (!syn) { 
		log("build: Synonyms need to be built before affinity");
		return true;
	}
	// Use default if server is blank
	if (!server || !gbstrlen(server)) server = g_conf.m_affinityServer;
	log(LOG_INFO, "build: rebuilding Thesaurus word affinity from "
		"server %s", server);
	m_rebuilding = true;
	StateAffinity *aff;
	try { aff = new (StateAffinity); }
	catch ( ... ) {
		log("build: Couldn't allocate %i bytes for thesaurus, "
			"aborting rebuild", sizeof(StateAffinity));
		m_rebuilding = false;
		return true;
	}
	g_thesaurus.m_affinityState = aff;
	mnew(aff, sizeof(StateAffinity), "thesaurus");
	memset(aff, 0, sizeof(StateAffinity));
	aff->m_time = gettimeofdayInMilliseconds();
	aff->m_synstart = syn;
	aff->m_syn = syn;
	aff->m_synend = synend;
	strncpy(aff->m_server, server, MAX_URL_LEN);
	aff->m_newTable = &aff->m_synTable;
	aff->m_newTable->set(0, NULL, 0, true);
	aff->m_oldTable = &m_synonymTable;
	aff->m_next = 1000;
	aff->m_thes = this;
	aff->m_fullRebuild = fullRebuild;
	aff->m_callback = gotAllAffinityPairs;
	char *c = strchr(server, ':');
	int32_t len;
	int32_t ip;
	if (c) 
		len = c - server;
	else
		len = gbstrlen(server);
	if (g_dns.getIp(server, len, &ip, 
			aff, gotAffinityIP,
			0, 30000))
		gotAffinityIP(aff, ip);
	return false;
}

bool Thesaurus::save() {
	char *p1 = m_synonymText, *p1end = m_synonymText + m_synonymLen;
	SafeBuf b;
	
	bool x = true;

	//x &= b.safePrintf("|lastRebuild|%"INT32"\n", m_lastRebuild);
	//x &= b.safePrintf("|numSynonyms|%"INT32"\n", m_numSynonyms);
	//x &= b.safePrintf("|totalPairs|%"INT32"\n", m_totalPairs);
	//x &= b.safePrintf("|totalSlots|%"INT32"\n", m_synonymTable.getNumSlots());

	while (p1 < p1end && x) {
		SynonymInfo syn;
		getSynonymInfo(p1, &syn);
		for (int32_t i = 0; i < syn.m_numSyns; i++) {
			char *p2 = syn.m_syn[i];
			int32_t a = syn.m_affinity[i];
			float af = a / (float)MAX_AFFINITY;
			int32_t f = syn.m_type[i];
			if (a >= 0) {
				x &= b.safePrintf("%s|%s|%f|%"INT32"\n", 
					p1, p2, af, f);
			} else {
				x &= b.safePrintf("%s|%s|%"INT32"|%"INT32"\n", 
					p1, p2, a, f);
			}
		}
		p1 += gbstrlen(p1) + 1;
	}

	char ff[PATH_MAX];
	snprintf(ff, PATH_MAX, "%s%sthesaurus.txt", g_hostdb.m_dir, s_dictDir);

	if (x) x &= (b.dumpToFile(ff) != 0);


	return x;
}

static char s_suffixEng[] = {
	"ational|ate\n"
	"ization|ize\n"
	"iveness|ive\n"
	"fulness|ful\n"
	"ousness|ous\n"
	"tional|tion\n"
	"ation|ate\n"
	"alism|al\n"
	"iment|y\n"
	"ator|ate\n"
	"ying|ie|ye|y\n"
	"ment|.\n"
	"sses|ss\n"
	"ings|e|.\n"
	"enci|ence\n"
	"anci|ance\n"
	"izer|ize\n"
	"ing|e|.\n"
	"ied|y\n"
	"men|man\n"
	"ies|y|i\n"
	"eed|ee\n"
	"bli|ble\n"
	"'re| are\n"
	" are|'re\n"
	"'ve| have\n"
	" have|'ve\n"
	"'ll| will\n"
	" will|'ll\n"
	"n't| not\n"
	" not|n't\n"
	"es|e|.\n"
	"ed|e|.\n"
	"'m| am\n"
	" am|'m\n"
	"'s| is|.\n"
	" is|'s\n"
	"'d| would| had\n"
	" would|'d\n"
	" had|'d\n"
	"s|.\n"
	"y|.\n"
};

bool Thesaurus::initStems() {

	// mdw: disable for now
	return true;

	char ff[256];

	// prevents certain words from stemming into the wrong thing
	// and provides stems for irregular words ("children" -> "child")

	sprintf(ff, "%s%sstemmer.exceptions", g_hostdb.m_dir, s_dictDir);
	if (!m_stemBuffer.fillFromFile(ff)) return false;
	char    *p = m_stemBuffer.getBufStart();
        char *pend = m_stemBuffer.getBuf();
	while (p < pend) {
		char *a = NULL, *b = NULL, *e = NULL;
		a = strchr(p, '|');
		b = strchr(p, '\n');
		if (a) e = strchr(a+1, '|');
		if (b) *b = '\0';
		if (!a || (e && e < b)) {
			log("query: Stem exception file is corrupt, line does "
				"not contain exactly one pipe: %s", p);
			m_stemBuffer.reset();
			m_stemTable.reset();
			break;
		}
		*a = '\0';
		uint32_t h1 = hash32n(p), h2 = hash32n(a+1);
		// add it in both ways now
		m_stemTable.addKey(h1, a+1);
		m_stemTable.addKey(h2, p);
		if (b) p = b+1;
		else   p = pend;
	}

	int32_t used = m_stemTable.getNumSlotsUsed();
	if (used) log(LOG_INIT, "query: Loaded %"INT32" stem exceptions", used);
	else      log(LOG_INIT, "query: Couldn't load stem exceptions");

	m_suffixBuffer.reset();
	m_suffixBuffer.safeMemcpy(s_suffixEng, sizeof(s_suffixEng) - 1);
	p    = m_suffixBuffer.getBufStart();
	pend = m_suffixBuffer.getBuf();
	while (p < pend) {
	// Count number of newlines, change all pipes to null bytes
		if (*p == '\n') m_numSuffixes++;
		if (*p == '|') { *p = '\0'; m_numReps++; }
		p++;
	}
	if (!m_numSuffixes || !m_numReps) {
		log("query: No suffixes or no replacements in %s", ff);
		return used != 0;
	} else {
		m_suffixes = (Suffix *)mmalloc(sizeof(Suffix) * m_numSuffixes,
			"stemmer");
		m_reps = (char **)mmalloc(sizeof(char *) * m_numReps, 
			"stemmer");
		m_repLens = (int32_t *)mmalloc(sizeof (int32_t) * m_numReps, 
			"stemmer");
		if (!m_suffixes || !m_reps || !m_repLens) {
			if (m_suffixes) mfree(m_suffixes, sizeof(Suffix) * 
						m_numSuffixes, "stemmer");
			if (m_reps)     mfree(m_reps, sizeof(char *) * 
						m_numReps, "stemmer");
			if (m_repLens)	mfree(m_repLens, sizeof(int32_t) * 
				m_numReps, "stemmer");
			m_suffixes = NULL;
			m_reps = NULL;
			m_repLens = NULL;
			log("query: Couldn't allocate memory for stemmer");
			return used != 0;
		}
	}
	p = m_suffixBuffer.getBufStart();
	Suffix *suf = m_suffixes;
	char **rep = m_reps;
	int32_t *repLenp = m_repLens;
	while (p < pend) {
		// first entry in a line is the suffix
		suf->m_suffix = p;
		int32_t len = gbstrlen(p);
		p += len;
		char *p2 = p + 1, *p2end;
		suf->m_numReps = 0;
		suf->m_len = len;
		suf->m_reps = rep;
		suf->m_repLens = repLenp;
		// count null bytes (from spaces) to get number of replacements
		while (*p != '\n' && p < pend) {
			if (*p == '\0') { 
				suf->m_numReps++; rep++; repLenp++; 
			}
			p++;
		}
		p2end = p;
		if (p2end < pend) *p2end = '\0';
		p++;
		// input validation
		if (!suf->m_numReps) {
			log("query: no replacement suffixes for %s",
				suf->m_suffix);
			continue;
		}
		char **rep2 = suf->m_reps;
		int32_t *repLenp2 = suf->m_repLens;
		while (p2 < p2end) {
			int32_t len = gbstrlen(p2);
		 	if (*p2 == '.') {
				*rep2 = "";
				*repLenp2 = 0;
			} else if (len) {
				*rep2 = p2;
				*repLenp2 = len;
			} else {
				log("query: zero-length replacement for %s",
					suf->m_suffix);
				continue;
			}
			rep2++;
			repLenp2++;
			p2 += len + 1;
		}
		suf++;
	}

	log(LOG_INIT, "query: Loaded suffixes");
	return true;
}

bool Thesaurus::load() {
	char ff[PATH_MAX], ff2[PATH_MAX];

	snprintf(ff, PATH_MAX, "%s%sthesaurus.txt", g_hostdb.m_dir, s_dictDir);
	snprintf(ff2, PATH_MAX, "%s%sthesaurus.dat", g_hostdb.m_dir, s_dictDir);
	struct stat stats;
	bool x = true;

	// do the reset up here so we don't have to do a table copy, and we can
	// just use m_synonymTable directly
	m_synonymTable.reset();

	SafeBuf b, b2;
	
	if (!b.fillFromFile(ff))
		return log("build: Couldn't load thesaurus from %s", ff);

	if (stat(ff, &stats)) return log("build: Could load, but couldn't "
		"stat %s", ff);

	// load in the additional buffer, "m_synonymText" as well as hash table,
	// because the offsets in the hash table reference that additional buffer.
	// it is fairly common for a hash table to do this, so it is built-in to
	// HashTableT::load()/save() now
	if ( m_synonymTable.load ( ff2 , &m_synonymText , &m_synonymLen ) && 
	     m_synonymTable.m_numSlots > 0 ) {
		// let gb know how many bytes to free...
		m_synonymSize = m_synonymLen;
		log(LOG_INFO,"admin: Loaded thesaurus from thesaurus.dat.");
		initStems();
		return true;
	}
	
	log(LOG_INIT, "build: Loading thesaurus from %s", ff);

	char *pstart = b.getBufStart(), *p = pstart, *pend = b.getBuf();

	
	SafeBuf synonymTextB;
	int32_t warn = 0;
	int32_t unknown = 0;

	// allow dups in this table
	m_synonymTable.setAllowDupKeys(true);

	// verify that there are exactly two pipes per line
	while (p < pend) {
		char *w1 = p, *w2 = NULL, *w3 = NULL, *w4 = NULL, *e = NULL;
		while (*p != '\n' && p < pend) p++;
		if (*p == '\n') *p++ = '\0';
		w2 = strchr(w1, '|');
		if (w2) w3 = strchr(w2 + 1, '|');
		if (w3) w4 = strchr(w3 + 1, '|');
		if (w4) e  = strchr(w4 + 1, '|');
		if (!w2 || !w3 || e) {
			log("build: Bad format in %s, line does not "
				"contain the right number of pipes: %s", 
				ff, w1);
			continue;
		} else {
			*w2++ = '\0';
			*w3++ = '\0';
			if (w4) *w4++ = '\0';
		}

		//int32_t w2len;
		int32_t a, b;
		//w1len = gbstrlen(w1);
		//w2len = gbstrlen(w2);
		a = strtol(w3, &e, 0);
		if (w4) b = strtol(w4, &e, 0);
		if (!w4 || b >= SYN_UNKNOWN ) {
			b = SYN_UNKNOWN;
			unknown++;
		}

		if (e && *e)
			log("build: Extra characters in affinity data: %s", e);

		uint64_t h1 = hash64Lower_utf8(w1), h2 = hash64Lower_utf8(w2);
		if (h1 == h2) {
			log(LOG_WARN, "build: Thesaurus pair has same hash "
				"(%s,%s)", w1, w2);
			continue;
		}
		int64_t v;
		// warp h2 since we don't want it matching h1 ever
		// because we are only adding it to the table to "save" the word ptr,
		// we are not adding it as a "synonym entry" per se. so this table
		// is really storing two different types of things.
		h2 ^= 0x987fce44;
		int32_t slot2 = m_synonymTable.getSlot(h2);
		int32_t offset2;
		if (slot2 < 0) {
			// point into our word buffer
			offset2 = synonymTextB.length();
			// copy our word into our word buffer
			x &= synonymTextB.safeMemcpy(w2, gbstrlen(w2) + 1);
			// . set the offset of the "word" with hash "h2"
			// . use a fake affinity of 0x7fffffff and a synd type of 8?
			v = 0x7FFFFFFF80000000LL + offset2;
			// only add this to the table because we want to "save" the
			// offset of its text for re-use
			x &= m_synonymTable.addKey(h2, v);
		} 
		// otherwise, we already stored it into the word buffer, recycle!
		else {
			v = m_synonymTable.getValueFromSlot(slot2);
			offset2 = OFFSET(v);
			// sanity check, affinity better be 0x7fffffff, otherwise
			// there might have been a collision?
			int32_t a2 = AFFINITY(v);
			if ( a2 != 0x7fffffff ) { char *xx = NULL; *xx = 0; }
		}
		// add the actual synonym info for the hash of word1, "h1"
		if (a < 0) warn++;
		// "a" is the synonym affinity
		v = ((int64_t)a << 32) + offset2;
		// b is the synonym type, see Thesaurus.h for these, #define'd
		v += (b << 27);
		x &= m_synonymTable.addKey(h1, v);
	}
	
	if (!x) return log("build: Thesaurus loading failure, memory low?");

	if (warn)
		log(LOG_INIT, "build: %"INT32" invalid/missing affinity "
		    "values, recommend rebuild", warn);
	if (unknown) 
		log(LOG_INIT, "build: %"INT32" synonyms with missing/"
		    "invalid type", unknown);
	// this no longer resets m_synonymTable, why did we
	// want to do that anyway??? MDW
	reset();

	// preserve the word buffer
	m_synonymText = synonymTextB.getBufStart();
	m_synonymLen  = synonymTextB.length();
	m_synonymSize = synonymTextB.getCapacity();
	synonymTextB.detachBuf();	// we own this now
	relabel(m_synonymText, m_synonymSize, "thesaurus");

	log(LOG_INIT,"build: Loaded %"INT32" synonym pairs.",
	    m_synonymTable.m_numSlotsUsed);

	// save it as "thesaurus.dat", and include the text buffer,
	// m_synonymText, that it references
	if ( ! g_conf.m_readOnlyMode ) 
		m_synonymTable.save(ff2,m_synonymText,m_synonymLen);

	initStems();

	return true;
}

