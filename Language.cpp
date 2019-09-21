#include "Language.h"
#include "sort.h"
#include "Speller.h"
#include "Sections.h"

// word/phrase must be in at least this many docs to be included in our dict
#define MIN_DOCS 3

// ROUTINES NEEDED FOR GBSORT
// The dict is stored as a tuple of ( original word, phonetic, (lang, score)..)
int cmpPhonet (const void *v1, const void *v2) {
	char *word1 = *(char **)v1;
	// phrase
	char *p1 = word1;
	// phonetic
	p1 += gbstrlen(p1) + 1;

	char *word2 = *(char **)v2;
	// phrase
	char *p2 = word2;
	// phonetic
	p2 += gbstrlen(p2) + 1;
	return strcmp(p1,p2);
}

int cmpScores (const void *v1, const void *v2) {
	Reco r1 = *(Reco *) v1;
	Reco r2 = *(Reco *) v2;
	return ( r1.score > r2.score );
}

int cmpFrnt (const void *v1, const void *v2) {
	// compare phrase
	char *p1 = *(char **) v1;
	char *p2 = *(char **) v2;
	return strcmp ( p1,p2 );
}

int cmpBck (const void *v1, const void *v2) {
	char *p1 = *(char **) v1;
	char *p2 = *(char **) v2;

	// string compare for reverse
	// go to the end
	p1 += gbstrlen(p1) - 1;
	p2 += gbstrlen(p2) - 1;
	while ( *p1 != '\0' && *p2 != '\0' ) {
		if ( *p1 > *p2 )
			return 1;
		else if ( *p1 < *p2 )
			return -1;
		p1--;
		p2--;
	}
	if ( *p1 == '\0' )
		return -1;
	if ( *p2 == '\0' )
		return 1;
	return 0;
}

static char s_keyMap[] = { 10, 24, 22, 12, 2, 13, 14, 15,  7, 16,
			   17, 18, 26, 25, 8, 9 , 0 , 3 , 11,  4,
			   6 , 23, 1 , 21, 5, 20 };
static char s_keyboard[] = {'q' ,'w','e','r','t','y','u','i','o' ,'p' ,
			     'a' ,'s','d','f','g','h','j','k','l' ,'\0',
			     'z','x','c','v','b','n','m','\0','\0','\0'};

//static void gotSummaryWrapper ( void *state );
//static void gotIndexListWrapper( void *state , RdbList *list );
//static void gotTermFreqsWrapper( void *state );
/*static void gotAffinityFreqs1Wrapper(void *state);
static void gotAffinityFreqs2Wrapper(void *state);*/


Language::Language(){
	m_rulesBuf = NULL;
	m_rulesBufSize = 0;
	m_rulesPtr = NULL;
	m_rulesPtrSize = 0;

	m_distributedBuf = NULL;
	m_distributedBufSize = 0;

	m_tuplePtr = NULL;
	m_tuplePtrSize = 0;
	
	m_narrowBuf = NULL;
	m_narrowBufSize = 0;

	m_numNarrowPtrs = 0;

	// Set to the default aspell parms
	m_editDistanceWeightsDel1 = 95;
	m_editDistanceWeightsDel2 = 95;
	m_editDistanceWeightsSwap = 90;
	m_editDistanceWeightsSub = 100;
	m_editDistanceWeightsSimilar = 10;
	m_editDistanceWeightsMin = 95;
	m_editDistanceWeightsMax = 100;
	m_soundslikeWeight = 15;
	m_wordWeight = 85;
	m_span = 50;

	// . set m_map
	// . this maps an ascii char to a char in dict space
	// . used in loadNarrow
	/*
	for ( int32_t i = 0 ; i < 256 ; i++ ) {
		unsigned char d = to_upper_ascii(i);
		if ( is_alpha(d) ) {
			// some like char 254 aren't really ascii!!
			// so make them into Z's, a rare letter, which
			// probably isn't in the same alphabet as 222 and 254
			if      ( d == 222 ) m_map[i] = 'Z' - 'A' + 12;
			else if ( d == 254 ) m_map[i] = 'Z' - 'A' + 12;
			else if ( d <  'A' ) m_map[i] = 38; // use apostrophes
			else if ( d >  'Z' ) m_map[i] = 38; // use apostrophes
			else    m_map[i] = d - 'A' + 12;
			continue;
		}
		if      ( is_digit(d) ) m_map[i] =  d - '0' +  2;
		else if ( d == 0      ) m_map[i] =  0;
		else if ( d == '\''   ) m_map[i] =  38;
		else if ( d == '-'    ) m_map[i] =  39;
		else if ( d == '\n'   ) m_map[i] =  0;
		else                    m_map[i] =  1; // a space
	}
	*/
	reset();
}

/*
bool Language::convertLatin1DictToUTF8( char *infile ){
	// open the file for reading
	FILE *fdr = fopen ( infile , "r" );
	if ( ! fdr )
		return log( "lang: Failed to open %s for reading: "	
			    "%s.",infile, strerror(errno) );
	char ff[1024];
	// open for writing	
	sprintf ( ff , "%s.utf8", infile );
	// delete it first
	unlink ( ff );
	// then open a new one for appending
	int fdw = open ( ff , 
			 O_CREAT | O_RDWR | O_APPEND ,
//			 S_IRUSR |S_IWUSR |S_IRGRP |S_IWGRP| S_IROTH);
	if ( fdw < 0 ){
		return log("lang: Could not open for %s "
			   "writing: %s.",ff, strerror(errno));
	}

	char  buf[1024];
	char  out[4*1024];
	// this loop goes through all the words and only adds those
	// words into the phonetic dict that have phonets.
	while ( fgets ( buf , 1024 , fdr ) ) {
		int32_t wlen = gbstrlen(buf);
		if ( wlen <= 0 || wlen > MAX_PHRASE_LEN )
			continue;
		// remove the newline \n
		buf [wlen - 1] = '\0';

		int32_t outLen = latin1ToUtf8(out, 4*1024, buf, gbstrlen(buf));
		// write out the trailing \n as well
		out[outLen] = '\n';
		outLen++;
		int32_t wn = write ( fdw , out , outLen ) ;
		if ( wn != outLen )
			return log("lang:  write: %s",
				   strerror(errno));
	}
	fclose(fdr);
	close(fdw);
	return true;
}
*/		

Language::~Language(){
	reset();
}

void Language::reset(){
	if ( m_rulesBuf && m_rulesBufSize > 0 ){
		mfree( m_rulesBuf, m_rulesBufSize, "LanguageBuf" );
		m_rulesBuf = NULL;
		m_rulesBufSize = 0;
	}
	if ( m_rulesPtr && m_rulesPtrSize > 0 ){
		mfree( m_rulesPtr, m_rulesPtrSize, "LanguagePtrBuf" );
		m_rulesPtr = NULL;
		m_rulesPtrSize = 0;
	}
	if ( m_distributedBuf && m_distributedBufSize > 0 ){
		mfree( m_distributedBuf, m_distributedBufSize, 
		       "DistributedPtrBuf" );
		m_distributedBuf = NULL;
		m_distributedBufSize = 0;
	}
	if ( m_tuplePtr && m_tuplePtrSize >0 ){
		mfree(m_tuplePtr, m_tuplePtrSize, "LanguageWordsPtr");
		m_tuplePtr = NULL;
		m_tuplePtrSize = 0;
	}
	if ( m_narrowBuf && m_narrowBufSize > 0 ){
		mfree(m_narrowBuf, m_narrowBufSize, "LanguageNarrowBuf");
		m_narrowBuf = NULL;
		m_narrowBufSize = 0;
	}
	m_numRules = 0;
	m_numTuples = 0;

	m_followup = true;
	m_collapseResult = false;
	m_removeAccents = true;
}

bool Language::init( char *unifiedBuf, int32_t unifiedBufSize, int32_t lang, 
		     int32_t hostsPerSplit, uint32_t myHash ){

	reset();

	if ( ! m_phonetics.set(256) ) return false;
	if ( ! m_dict.set(256)      ) return false;
	if ( ! m_distributedPopPhrases.set(256) ) return false;
	
	
	m_lang = lang;
	m_charset = getLanguageCharset(m_lang);

	// load the hashtable for getPhrasePopularity
	//if ( !loadDict() )
		

	// load the rules dictionary
	if ( !loadRules( ) || 
	     !loadSpellerDict( unifiedBuf, unifiedBufSize, hostsPerSplit, 
			       myHash ) ){
		log ( LOG_INIT,"lang: Error initializing for "
		      "language %s", getLanguageAbbr(m_lang) );
		return false;
	}
	//if ( g_conf.m_doNarrowSearch && 
	//     !loadNarrow( unifiedBuf, unifiedBufSize, hostsPerSplit, myHash) ){
	//	log ( LOG_INIT,"lang: Error initializing narrow search for "
	//	      "language %s", getLanguageAbbr(m_lang) );
	//	// don't return since this isn't critical
	//	//return false
	//}
	return true;
}

///////////////////////////////////////////////////////
// DICTIONARY LOADING ROUTINES BELOW HERE
//
// These will load g_hostdb.m_dir/dict/ files from
///////////////////////////////////////////////////////

bool Language::loadRules ( ) {
	char ff[1024];
	File f;
	sprintf ( ff , "%sdict/%s/%s_phonet.dat", g_hostdb.m_dir,
		  getLanguageAbbr(m_lang), getLanguageAbbr(m_lang));
	f.set ( ff );

	// open file
	if ( ! f.open ( O_RDONLY ) ) {
		log("lang: open: %s",mstrerror(g_errno)); 
		return false; 
	}
	
	// get file size
	int32_t fileSize = f.getFileSize() ;

	// store a \0 at the end
	m_rulesBufSize = fileSize + 1;

	// make buffer to hold all
	m_rulesBuf = (char *) mmalloc( m_rulesBufSize, "LanguageBuf" );
	if ( !m_rulesBuf ) {
		g_errno = ENOMEM;
		log("lang: mmalloc: %s",mstrerror(errno));
		return false;
	}

	// read em all in
	if ( ! f.read ( m_rulesBuf , fileSize , 0 ) ) {
		log("lang: read: %s", mstrerror(g_errno));
		return false;
	}

	m_rulesBuf[fileSize] = '\0';

	// change \n to \0
	for ( int32_t i = 0 ; i < m_rulesBufSize ; i++ ) {
		if ( m_rulesBuf[i] != '\n' )
			continue;
		m_rulesBuf[i] = '\0';
	}

	f.close();

	m_numRules = 0;
	char *p = m_rulesBuf;

	// This loop checks how many rules we have
	while ( p < ( m_rulesBuf + m_rulesBufSize ) ){
		// if it is a comment, skip
		// if no line, skip
		if ( *p == '#' ||  gbstrlen(p) == 0 || *p == ' ' ){
			p += gbstrlen(p) + 1;
			continue;
		}
		// we have a tuple
		if ( strstr(p, "followup") == p ){
			while ( *p != ' ' )
				p++;
			while ( *p == ' ' )
				p++;
			if ( *p != '1' )
				m_followup = false;
		}
		else if ( strstr(p, "collapse_result") == p ){
			while ( *p != ' ' )
				p++;
			while ( *p == ' ' )
				p++;
			if ( *p == '1' )
				m_collapseResult = true;
		}			
		else if ( strstr(p, "version") == p ){
			while ( *p != ' ' )
				p++;
			while ( *p == ' ' )
				p++;
			if ( *p != '1' )
				m_removeAccents = false;
		}
		
		// else the rules start or end here
		else 
			m_numRules += 2;

		p += gbstrlen(p) + 1;
	}

	// allocate memory for the ruleptrs
	m_rulesPtrSize = m_numRules * sizeof ( char* ) * m_numRules;
	
	m_rulesPtr = (char **) mmalloc(m_rulesPtrSize,"LanguagePtrBuf");

	if ( !m_rulesPtr ){
		g_errno = ENOMEM;
		log("lang: mmalloc: %s",mstrerror(errno));
		return false;
	}		
	
	// init
	for ( int32_t i = 0; i < MAX_CHARS; i++) {
		m_ruleStarts[i] = -1;
		m_ruleChars[i] = false;
	}

	// do the loop again and assign the pointers	
	p = m_rulesBuf;
	int32_t numRules = 0;
	while ( p < ( m_rulesBuf + m_rulesBufSize ) ){
		char *start = p;
		// if it is a comment, skip
		// if no line, skip
		if ( *p == '#' ||  gbstrlen(p) == 0 || *p == ' ' ){
			p += gbstrlen(p) + 1;
			continue;
		}

		// we have a tuple
		while ( *p != ' ' )
			p++;
		while ( *p == ' ' ){
			*p = '\0';
			p++;
		}

		// if the rule converts a letter into a '_' (blank)
		if ( *p == '_' )
			*p = '\0';
		
		if ( strstr(start, "followup") == start ){
			if ( *p != '1' )
				m_followup = false;
		}
		else if ( strstr(start, "collapse_result") == start ){
			if ( *p == '1' )
				m_collapseResult = true;
		}			
		else if ( strstr(start, "version") == start ){
			if ( *p != '1' )
				m_removeAccents = false;
		}
		// else the rules start or end here
		else{			
			m_rulesPtr[numRules++] = start;
			m_rulesPtr[numRules++] = p;
			// mark the chars that occur in the rule
			// lets just mark the first char. It seems to suffice
			if ( *p )
			  m_ruleChars[(int32_t)*p] = true;
		}
		p += gbstrlen(p) + 1;
	}
	
	// m_ruleStarts[i] points to the index of the m_rulesPtr where the 
	// rule of character i starts
	for ( int32_t i = 0; i < numRules; i += 2) {
		int32_t k = (UChar8) m_rulesPtr[i][0];
		if  ( m_ruleStarts[k] < 0 )
			m_ruleStarts[k] = i;
	}
	//	if ( m_lang == 2 || m_lang == 3 ) makeDict();
	return true;
}

bool Language::loadSpellerDict( char *spellerBuf, int32_t spellerBufSize, 
				int32_t hostsPerSplit, uint32_t myHash ){
	File distributedPopFile;
	char ff[1024];
	// load the distributed pop file
	sprintf ( ff , "%sdict/%s/%s.query.phonet.%"INT32"", g_hostdb.m_dir,
		  getLanguageAbbr(m_lang), getLanguageAbbr(m_lang), myHash);
	distributedPopFile.set ( ff );
	if ( ! distributedPopFile.open ( O_RDONLY ) ) {
		log("lang: open: %s. Generating from common pop file",
		    mstrerror(g_errno)); 
		sprintf ( ff , "%sdict/%s/%s.query.phonet", g_hostdb.m_dir,
			  getLanguageAbbr(m_lang), getLanguageAbbr(m_lang));
		// If we don't have the distributed pop file, open the
		// common pop file and generate the distributed one
		if ( !genDistributedPopFile( ff, myHash ))
			return false;
		// try opening the file now
		if ( ! distributedPopFile.open ( O_RDONLY ) ) {
			log("lang: open: %s",mstrerror(g_errno)); 
			return false; 
		}
	}
	
	// get file sizes
	int32_t distributedPopFileSize = distributedPopFile.getFileSize();

	// store a \0 at the end
	m_distributedBufSize = distributedPopFileSize + 1;

	// make buffer to hold all
	m_distributedBuf = (char *) mmalloc(m_distributedBufSize,
					    "DistributedPtrBuf");
	if ( !m_distributedBuf) {
		log("lang: mmalloc: %s",mstrerror(errno));return false;
	}

	char *p = m_distributedBuf;
	// read em all in	
	if ( ! distributedPopFile.read ( p , distributedPopFileSize , 0 ) ){
		log("lang: read: %s", mstrerror(g_errno));
		return false;
	}
	m_distributedBuf[distributedPopFileSize] = '\0';

	distributedPopFile.close();

	// count the tuples that belong to this language that come from
	// the wordlist and query file (i.e. that are not negative )
	p = spellerBuf;
	while ( p < spellerBuf + spellerBufSize - 1){
		// first is the phrase
		char *phrase = p;
		// if line is a comment skip it
		if ( *p == '#' ){
			p += gbstrlen(p) + 1;
			continue;
		}
		// skip phrase and move to phonet
		p += gbstrlen(p) + 1 ;
		char *phonet = p;

		if ( p >= spellerBuf + spellerBufSize-1 ) break;
		// skip phonet and move to (lang,score) tuples
		p += gbstrlen(p) + 1;

		if ( p >= spellerBuf + spellerBufSize-1 ) break;
		// skip (lang, score) tuple 
		p += gbstrlen(p) + 1;

		// check if phonet it present
		if ( *phonet == '\0' )
			continue;
		uint64_t phonetKey = hash64Lower_utf8(phonet);
		// check if this phonet belongs to this host
		if ( phonetKey % hostsPerSplit != myHash )
			continue;

		uint64_t h = hash64d(phrase, gbstrlen(phrase));

		// check if this phrase belongs to this language
		// can do that by calling spellers getphrasepopularity
		if ( g_speller.getPhrasePopularity( phrase, h, false, 
						    m_lang ) <= 0 )
			continue;

		m_numTuples++;
	}

	// also change the \t to \0
	p = m_distributedBuf;
	while ( p < m_distributedBuf + m_distributedBufSize ){
		m_numTuples++;
		while ( *p != '\n' && 
			p < m_distributedBuf + m_distributedBufSize - 1) {
			if ( *p == '\t' )
				*p = '\0';
			p++;
		}
		*p = '\0';
		p++;
	}
	
	// tuples have already been counted 
	m_tuplePtrSize = m_numTuples * sizeof(char *);
	m_tuplePtr = (char **) mmalloc ( m_tuplePtrSize, "LanguageTuplePtr" );
	if ( !m_tuplePtr ) {
		log("lang: mmalloc: %s",mstrerror(errno));return false;}

	int32_t numTuples = 0;
	
	// now go through the unified dict again and assign the pointers
	p = spellerBuf;
	while ( p < spellerBuf + spellerBufSize - 1){
		// first is the phrase
		char *phrase = p;
		// if line is a comment skip it
		if ( *p == '#' ){
			p += gbstrlen(p) + 1;
			continue;
		}
		// skip phrase and move to phonet
		p += gbstrlen(p) + 1;
		char *phonet = p;

		if ( p >= spellerBuf + spellerBufSize - 1 ) break;
		// skip phonet and move to (lang,score) tuples
		p += gbstrlen(p) + 1;

		if ( p >= spellerBuf + spellerBufSize - 1 ) break;
		// skip (lang, score) tuple 
		p += gbstrlen(p) + 1;

		if ( *phonet == '\0' )
			continue;

		uint64_t phonetKey = hash64Lower_utf8(phonet);
		// check if this phonet belongs to this host
		if ( phonetKey % hostsPerSplit != myHash )
			continue;

		uint64_t h = hash64d(phrase, gbstrlen(phrase));

		// check if this phrase belongs to this language
		// can do that by calling spellers getphrasepopularity
		if ( g_speller.getPhrasePopularity( phrase, h, false, 
					  m_lang ) <= 0 )
			continue;

		m_tuplePtr[numTuples] = phrase;
		numTuples++;
	}

	// go through the distributed dict and assign the pointers
	p = m_distributedBuf;
	while ( p < m_distributedBuf + m_distributedBufSize ){
		m_tuplePtr[numTuples++] = p;
		// skip phrase
		p += gbstrlen(p) + 1;

		if ( p >= m_distributedBuf + m_distributedBufSize ) break;
		// skip phonet
		p += gbstrlen(p) + 1;

 		if ( p >= m_distributedBuf + m_distributedBufSize ) break;
		// skip popularity
		p += gbstrlen(p) + 1;
	}


	// sanity
	for ( int32_t j = 0 ; j< numTuples ; j++ )
		gbstrlen(m_tuplePtr[j]) ;

	// sanity check
	if ( numTuples != m_numTuples ){
		char *xx = NULL; *xx = 0;
	}

	// kill last one seems problemtic with #define EFENCE in Mem.cpp
	numTuples--;
	m_numTuples--;

	// sort the wordsPtrs accoding to their phonetics
	gbsort( m_tuplePtr, m_numTuples, sizeof(char*), cmpPhonet );

	char *tuple;
	m_numPhonets = 0;
	int32_t startIndex = 0;
	int32_t index = 0;
	while ( index < m_numTuples ) {
		// The distributed dict is stored as a tuple of 
		// ( original phrase, phonetic, lang, score )
		// first to come is the phrase
		tuple = m_tuplePtr[index];

		// move to the phonet
		tuple += gbstrlen(tuple) + 1;
		
		uint64_t phonetKey = hash64Lower_utf8 ( tuple );
		if ( phonetKey % hostsPerSplit != myHash ){
			index++;
			continue;
		}
		int32_t numWordsInPhonet = 0;
		startIndex = index;
		while ( index < m_numTuples ){
			// first to come is the phrase
			tuple = m_tuplePtr[index];
			char *phrase = m_tuplePtr[index];

			// move to the phonet
			tuple += gbstrlen(tuple) + 1;
			
			uint64_t pKey = hash64Lower_utf8(tuple);
			if ( pKey != phonetKey )
				break;

			// move to the popularity
			tuple += gbstrlen(tuple) + 1;

			// only add the distributed pop words if they come
			// out of the distributed pop words dict
			if (phrase > m_distributedBuf && 
			    phrase < m_distributedBuf + m_distributedBufSize){
				// add the distributed pop words
				uint64_t h = hash64d( phrase,
							       gbstrlen(phrase));
				int32_t slot = m_distributedPopPhrases.
					getSlot(h);
				int32_t pop = atoi(tuple);
				if ( slot == -1 )
					m_distributedPopPhrases.addKey(h, pop);
			}
			numWordsInPhonet++;
			index++;
		}
		       
		int32_t slot = m_phonetics.getSlot ( phonetKey );
		if ( slot != -1 ){
			log(LOG_LOGIC, "speller: %"INT32" != -1, %16"XINT64", %s", 
				slot, phonetKey, tuple);
			char *xx = NULL; *xx = 0;
		}
		
		// make the composite value
		uint64_t value = startIndex;
		// make it the higher 32 bits
		value <<= 32;
		value += numWordsInPhonet;
	       
		m_phonetics.addKey( phonetKey, value );
		m_numPhonets++;
	}
		
	log(LOG_INIT,"lang: Read %"INT32" words and %"INT32" phonets into memory",
	    m_numTuples, m_numPhonets );
	return true;
}


/*
bool Language::loadNarrow( char *spellerBuf, int32_t spellerBufSize, 
			   int32_t hostsPerSplit, uint32_t myHash ){
	// don't load for any other language except english
	if ( m_lang != langEnglish )
		return true;

	// first find out how many phrases have more than 1 word
	// count the tuples that belong to this language that come from
	// the wordlist and query file (i.e. that are not negative )
	char *p = spellerBuf;
	while ( p < spellerBuf + spellerBufSize - 1){
		// first is the phrase
		char *phrase = p;
		// if line is a comment skip it
		if ( *p == '#' ){
			p += gbstrlen(p) + 1;
			continue;
		}
		// skip phrase and move to phonet
		p += gbstrlen(p) + 1;
		char *phonet = p;
		// skip phonet and move to (lang,score) tuples
		p += gbstrlen(p) + 1;
		// skip (lang, score) tuple 
		p += gbstrlen(p) + 1;

		uint64_t h = hash64d(phrase, gbstrlen(phrase));

		// check if this phrase belongs to this language
		// can do that by calling spellers getphrasepopularity
		if ( g_speller.
		     getPhrasePopularity( phrase, h, false, m_lang ) <= 0 ){
			continue;
		}

		// check if phonet it present
		if ( *phonet == '\0' ){
			continue;
		}
		uint64_t phonetKey = hash64Lower_utf8(phonet);

		// check if this phonet belongs to this host
		if ( phonetKey % hostsPerSplit != myHash ){
			continue;
		}

		// make sure the phrase has 3 or more letters
		if ( gbstrlen(phrase) < 3 )
			continue;

		// check if the phrase has more than 1 word
		bool isPhrase = false;
		char *q = phrase;
		while ( *q != '\0' ){
			if ( *q == ' ' )
				isPhrase = true;
			q++;
		}
		if ( !isPhrase )
			continue;

		m_numNarrowPtrs++;
	}

	p = m_distributedBuf;
	while ( p < m_distributedBuf + m_distributedBufSize ){
		// first is the phrase
		char *phrase = p;
		// if line is a comment skip it
		if ( *p == '#' ){
			p += gbstrlen(p) + 1;
			continue;
		}
		// skip phrase and move to phonet
		p += gbstrlen(p) + 1;
		// skip phonet
		p += gbstrlen(p) + 1;
		// skip popularity
		p += gbstrlen(p) + 1;
		// make sure the phrase has 3 or more letters
		if ( gbstrlen(phrase) < 3 )
			continue;

		// check if the phrase has more than 1 word
		bool isPhrase = false;
		char *q = phrase;
		while ( *q != '\0' ){
			if ( *q == ' ' )
				isPhrase = true;
			q++;
		}
		if ( !isPhrase )
			continue;

		m_numNarrowPtrs++;
	}

	// allocate memory for that
	// also allocate memory for the m_frntCharPtrs and m_bckCharPtrs
	m_narrowBufSize = 2 * sizeof (char *) * m_numNarrowPtrs + 
		( NUM_CHARS * NUM_CHARS * NUM_CHARS * 4 * 2 );
	m_narrowBuf = (char *) mmalloc( m_narrowBufSize, "LanguageNarrowBuf" );
	if ( !m_narrowBuf ){
		log("lang: Could not allocate %"INT32" bytes for narrow buf",
		    m_narrowBufSize);
		g_errno = ENOMEM;
		return false;
	}

	p = m_narrowBuf;
	m_frntPtrs = (char **) p;
	p += sizeof(char **) * m_numNarrowPtrs;
	m_bckPtrs = (char **) p;
	p += sizeof(char *) * m_numNarrowPtrs;
	m_frntCharPtrs = (int32_t *) p;
	p += NUM_CHARS * NUM_CHARS * NUM_CHARS * 4;
	m_bckCharPtrs = (int32_t *)p;
	p += NUM_CHARS * NUM_CHARS * NUM_CHARS * 4;

	int32_t numNarrowPtrs = 0;
	// go through the loop again and set the positions
	p = spellerBuf;
	while ( p < spellerBuf + spellerBufSize - 1){
		// first is the phrase
		char *phrase = p;
		// if line is a comment skip it
		if ( *p == '#' ){
			p += gbstrlen(p) + 1;
			continue;
		}
		// skip phrase and move to phonet
		p += gbstrlen(p) + 1;
		char *phonet = p;
		// skip phonet and move to (lang,score) tuples
		p += gbstrlen(p) + 1;
		// skip (lang, score) tuple 
		p += gbstrlen(p) + 1;

		uint64_t h = hash64d(phrase, gbstrlen(phrase));

		// check if this phrase belongs to this language
		// can do that by calling spellers getphrasepopularity
		if ( g_speller.
		     getPhrasePopularity( phrase, h, false, m_lang ) <= 0 ){
			continue;
		}

		// check if phonet it present
		if ( *phonet == '\0' ){
			continue;
		}
		uint64_t phonetKey = hash64Lower_utf8(phonet);

		// check if this phonet belongs to this host
		if ( phonetKey % hostsPerSplit != myHash ){
			continue;
		}

		// make sure the phrase has 3 or more letters
		if ( gbstrlen(phrase) < 3 )
			continue;

		// check if the phrase has more than 1 word
		bool isPhrase = false;
		char *q = phrase;
		while ( *q != '\0' ){
			if ( *q == ' ' )
				isPhrase = true;
			q++;
		}
		if ( !isPhrase )
			continue;

		m_frntPtrs[numNarrowPtrs] = phrase;
		m_bckPtrs[numNarrowPtrs] = phrase;
		numNarrowPtrs++;
	}

	p = m_distributedBuf;
	while ( p < m_distributedBuf + m_distributedBufSize ){
		// skip phrase
		char *phrase = p;
		// if line is a comment skip it
		if ( *p == '#' ){
			p += gbstrlen(p) + 1;
			continue;
		}
		p += gbstrlen(p) + 1;
		// skip phonet
		p += gbstrlen(p) + 1;
		// skip popularity
		p += gbstrlen(p) + 1;
		// make sure the phrase has 3 or more letters
		if ( gbstrlen(phrase) < 3 )
			continue;

		// check if the phrase has more than 1 word
		bool isPhrase = false;
		char *q = phrase;
		while ( *q != '\0' ){
			if ( *q == ' ' )
				isPhrase = true;
			q++;
		}
		if ( !isPhrase )
			continue;

		m_frntPtrs[numNarrowPtrs] = phrase;
		m_bckPtrs[numNarrowPtrs] = phrase;
		numNarrowPtrs++;
	}

	// sanity check
	if ( numNarrowPtrs != m_numNarrowPtrs ){
		log(LOG_LOGIC, "speller: %"INT32" != %"INT32" numNarrowPtrs",
			numNarrowPtrs, m_numNarrowPtrs);
		char *xx=NULL; *xx=0;
	}
	// sort the front pointers and back pointers
	gbsort ( m_frntPtrs, m_numNarrowPtrs, sizeof(char*), cmpFrnt );
	gbsort ( m_bckPtrs, m_numNarrowPtrs, sizeof(char*), cmpBck );

	// printing them out
	//for ( int32_t i = 0; i < m_numNarrowPtrs; i++ )
	//	log ( "lang: frnt=%s\t\t bck=%s", 
	//      m_frntPtrs[i] + gbstrlen(m_frntPtrs[i]) + 1, 
	//      m_bckPtrs[i] + gbstrlen(m_bckPtrs[i]) + 1);

	// now set the m_frntCharPtrs and m_bckCharPtrs
	for ( int32_t i = 0; i < NUM_CHARS * NUM_CHARS * NUM_CHARS; i++ ){
		m_frntCharPtrs[i] = -1;
		m_bckCharPtrs[i] = -1;
	}
	for ( int32_t i = 0; i < m_numNarrowPtrs; i++ ){
		// align to the phrase
		char *frnt = m_frntPtrs[i];
		char *bck = m_bckPtrs[i];
		bck += gbstrlen(bck) - 1;

		char f0 = to_dict_char(frnt[0]);
		char f1 = to_dict_char(frnt[1]);
		char f2 = to_dict_char(frnt[2]);
		char b0 = to_dict_char(bck[0]);
		char b1 = to_dict_char(bck[-1]);
		char b2 = to_dict_char(bck[-2]);

		int32_t fx = f0 * NUM_CHARS * NUM_CHARS + f1 * NUM_CHARS + f2;
		int32_t bx = b0 * NUM_CHARS * NUM_CHARS + b1 * NUM_CHARS + b2;
		if ( m_frntCharPtrs[fx] == -1 )
			m_frntCharPtrs[fx]= i;
		if ( m_bckCharPtrs[bx] == -1 )
			m_bckCharPtrs[bx] = i;
	}
	return true;
}
*/

bool Language::loadDictHashTable( ){
	char ff[MAX_FRAG_SIZE];
	// first load the language dict
	// open the input file
	FILE *fdr;
	sprintf ( ff , "%sdict/%s/%s.wl.phonet", g_hostdb.m_dir,
		  getLanguageAbbr(m_lang), getLanguageAbbr(m_lang) );
	// then open 
	fdr = fopen ( ff, "r" );
	if ( !fdr )
		return log("lang: Could not open %s for reading: "
			   "%s.", ff, strerror(errno));
	
	char  buf[1024];
	
	// this loop goes through all the words 
	while ( fgets ( buf , 1024 , fdr ) ) {
		int32_t wlen = gbstrlen(buf);
		if ( wlen <= 0 || wlen > MAX_PHRASE_LEN )
			continue;
		// remove the newline \n
		buf [wlen - 1] = '\0';
		char *p = buf;
		int32_t pop = atoi(p);
		// move to the phrase
		while ( *p != '\t' )
			p++;
		p++;
		char *phrase = p;
		// move to the next tab before the phonetic
		while ( *p != '\t' )
			p++;
		
		uint64_t key = hash64d( phrase, p - phrase);
		int32_t slot = m_dict.getSlot(key);
		
		int32_t value = 0;
		if ( slot != -1 ){
			value = m_dict.getValueFromSlot(slot);
			if ( pop < value )
				continue;
		}
		m_dict.addKey( key, pop );
	}
	fclose(fdr);
	
	// now for the top pop words from the query log
	sprintf ( ff , "%sdict/%s/%s.query.phonet.top", g_hostdb.m_dir,
		  getLanguageAbbr(m_lang), getLanguageAbbr(m_lang) );
	// then open 
	fdr = fopen ( ff, "r" );
	if ( !fdr )
		return log("lang: Could not open %s for reading: "
			   "%s.", ff, strerror(errno));
	
	// this loop goes through all the words 
	while ( fgets ( buf , 1024 , fdr ) ) {
		int32_t wlen = gbstrlen(buf);
		if ( wlen <= 0 || wlen > MAX_PHRASE_LEN )
			continue;
		// remove the newline \n
		buf [wlen - 1] = '\0';
		char *p = buf;
		int32_t pop = atoi(p);
		// move to the phrase
		while ( *p != '\t' )
			p++;
		p++;
		char *phrase = p;
		// move to the next tab before the phonetic
		while ( *p != '\t' )
			p++;
		
		uint64_t key = hash64d( p, p - phrase);
		int32_t slot = m_dict.getSlot(key);
		
		int32_t value = 0;
		if ( slot != -1 ){
			value = m_dict.getValueFromSlot(slot);
			if ( pop < value )
				continue;
		}
		m_dict.addKey( key, pop );
	}
	fclose(fdr);


	// now for the title rec dicts. If the phrase is only present in the
	// titlerec dict then store it as a negative value
	for ( int32_t i = 0; i < NUM_CHARS; i++ ){
		// open the input file
		FILE *fdr;
		sprintf ( ff , "%sdict/%s/%s.dict.%"INT32"", g_hostdb.m_dir,
			  getLanguageAbbr(m_lang), getLanguageAbbr(m_lang), i);
		// then open 
		fdr = fopen ( ff, "r" );
		if ( !fdr )
			return log("lang: Could not open %s for reading: "
				   "%s.", ff, strerror(errno));
	
		// this loop goes through all the words and only adds those
		// words into the phonetic dict that have phonets.
		while ( fgets ( buf , 1024 , fdr ) ) {
			int32_t wlen = gbstrlen(buf);
			if ( wlen <= 0 || wlen > MAX_PHRASE_LEN )
				continue;
			// remove the newline \n
			buf [wlen - 1] = '\0';
			char *p = buf;
			int32_t pop = ( atoi(p) * 32000 )/ 10000;
			// move to the phrase
			while ( *p != '\t' )
				p++;
			p++;
			uint64_t key = hash64d( p, gbstrlen(p) );
			// add only if it is not found in english dict and 
			// query dict
			int32_t slot = m_dict.getSlot(key);

			int32_t value = 0;
			if ( slot != -1 ){
				value = m_dict.getValueFromSlot(slot);
				if ( pop < value )
					continue;
			}
			// if phrase is only present in the title rec, store
			// as a negative value
			else
				pop *= -1;
			
			m_dict.addKey( key, pop );
		}
		fclose(fdr);
	}
	return true;
}

bool Language::loadWikipediaWords(){
	// open the wikipedia file
	char ff[1024];
	sprintf ( ff , "%sdict/%s/%s.wiki", g_hostdb.m_dir,
		  getLanguageAbbr(m_lang), getLanguageAbbr(m_lang));

	FILE *fdr = fopen ( ff, "r" );
	if ( ! fdr ) {
		return log("lang: Could not open for mispelled words"
			   "reading: %s.",strerror(errno));
	}

	m_wiki.set(1024);
	char buf[1024];
	// go through the words in dict/words
	while ( fgets ( buf , 1024 , fdr ) ) {
		// length of word(s), including the terminating \n
		int32_t wlen = gbstrlen(buf) ;
		// skip if empty
		if ( wlen <= 0 ) continue;
		buf[wlen-1]='\0';
		uint32_t key = hash32d(buf, gbstrlen(buf));
		int32_t slot = m_wiki.getSlot ( key );
		if ( slot != -1 ){
			continue;
			char *xx=NULL; *xx=0;
		}
		m_wiki.addKey(key,1);
	}
	fclose(fdr);
	return true;
}


bool Language::loadMispelledWords(){
	char ff [1024];
	// also open the commonly misspelled words file
	sprintf ( ff , "%sdict/%s/%s.misp", g_hostdb.m_dir,
		  getLanguageAbbr(m_lang), getLanguageAbbr(m_lang));
	FILE *fdr = fopen ( ff, "r" );
	if ( ! fdr ) {
		return log("lang: Could not open for mispelled words"
			   "reading: %s.",strerror(errno));
	}

	m_misp.set(1024);
	char buf[1024];
	// go through the words in dict/words
	while ( fgets ( buf , 1024 , fdr ) ) {
		// length of word(s), including the terminating \n
		int32_t wlen = gbstrlen(buf) ;
		// skip if empty
		if ( wlen <= 0 ) continue;
		buf[wlen-1]='\0';
		uint32_t key = hash32d(buf, gbstrlen(buf));
		int32_t slot = m_misp.getSlot ( key );
		if ( slot != -1 ){
			char *xx=NULL; *xx=0;
		}
		m_misp.addKey(key,1);
	}

	fclose(fdr);
	return true;
}


///////////////////////////////////////////////////////
// LANGUAGE RECOMMENDATION ROUTINES BELOW HERE
//
///////////////////////////////////////////////////////

/*
int32_t Language::narrowPhrase ( char *request, char *phrases, int32_t *pops, 
			     int32_t maxPhrases ){
	// if we haven't been loaded, just return
	if ( m_numNarrowPtrs == 0 )
		return 0;

	int32_t numPhrases = 0;

	int32_t requestLen = gbstrlen(request);
	// don't check for narrow phrase if the original phrase is more than
	// MAX_PHRASE_LEN - 3 OR less than 3 chars.
	// Why MAX_PHRASE_LEN - 3 ? Because then only can we find a narrow 
	// phrase
	if ( requestLen >  MAX_PHRASE_LEN - 3 || requestLen < 3 )
		return numPhrases;

	// get the start and end two chars and convert them to dict_char
	char f0 = to_dict_char(request[0]);
	char f1 = to_dict_char(request[1]);
	char f2 = to_dict_char(request[2]);
	char *bck = request + requestLen - 1;
	char b0 = to_dict_char(bck[0]);
	char b1 = to_dict_char(bck[-1]);
	char b2 = to_dict_char(bck[-2]);

	uint64_t start = gettimeofdayInMilliseconds();
	int32_t minPop = 0;
	char req[MAX_PHRASE_LEN];
	// first get all the ones in the front
	strcpy(req, request);
	// add a space so that we match the exact phrase
	req[requestLen] = ' ';
	req[requestLen + 1] = '\0';
	int32_t fx = f0 * NUM_CHARS * NUM_CHARS + f1 * NUM_CHARS + f2;
	int32_t index = m_frntCharPtrs[fx];
	if ( index == -1 )
		goto skipFrnt;
	while ( index < m_numNarrowPtrs ){
		char *tuple = m_frntPtrs[index++];

		char *phrase = tuple;
		//check if we have gone over the phrase (if present) or not
		int32_t cmp = strncasecmp (phrase, req, gbstrlen(req));
		if ( cmp > 0 )
			break;
		if ( cmp < 0 )
			continue;

		// found it. get the popularity
		int32_t pop = 0;
		// if its from the distributed dict, get it directly
		if ( tuple > m_distributedBuf && 
		     tuple < m_distributedBuf + m_distributedBufSize ){ 
			// skip the phrase
			tuple += gbstrlen(tuple) + 1;
			// skip the phonet
			tuple += gbstrlen(tuple) + 1;
			pop = atoi(tuple);
		}
		// else get it by getphrasePopularity 
		else {
			uint64_t h = hash64d(phrase, gbstrlen(phrase));
			pop = g_speller.getPhrasePopularity(phrase, h, false, 
							    m_lang);
		}

		int32_t indx = numPhrases;
		// if not full
		if ( numPhrases < maxPhrases )
			numPhrases++;
		// if full 
		else{
			if ( minPop >= pop )
				continue;
			int32_t minIndx = 0;
			minPop = pops[0];
			for ( int32_t j = 1; j < maxPhrases; j++ ){
				if ( minPop < pops[j] )
						continue;
				minPop = pops[j];
				minIndx = j;
			}
			if ( minPop >= pop )
				continue;
			indx = minIndx;
			minPop = pop;
		}
		
		// store the pop
		pops[indx] = pop;
		strcpy ( &phrases[MAX_FRAG_SIZE * indx],phrase );
		log (LOG_DEBUG,"speller: Narrow phrase=%s, pop=%"INT32"",
		     &phrases[MAX_FRAG_SIZE * indx], pops[indx]);
	}

 skipFrnt:
	// now get the back
	req[0] = ' ';
	strcpy(&req[1],request);
	int32_t bx = b0 * NUM_CHARS * NUM_CHARS + b1 * NUM_CHARS + b2;
	index = m_bckCharPtrs[bx];
	if ( index == -1 )
		return numPhrases;
	while ( index < m_numNarrowPtrs ){
		char *tuple = m_bckPtrs[index++];

		char *phrase = tuple;
		//check if we have gone over the phrase (if present) or not
		// cannot use strcasecmp because we compare from the back
		char *p1 = phrase + gbstrlen(phrase) - 1;
		char *p2 = req + gbstrlen(req) - 1;
		while ( p1 >= phrase && p2 >= req ) {
			if ( *p1 != *p2 )
				break;
			p1--;
			p2--;
		}

		if ( p2 >= req || p1 < phrase ){
			if ( *p1 > *p2 )
				break;
			continue;
		}

		// found it
		int32_t pop = 0;
		// if its from the distributed dict, get it directly
		if ( tuple > m_distributedBuf && 
		     tuple < m_distributedBuf + m_distributedBufSize ){ 
			// skip the phrase
			tuple += gbstrlen(tuple) + 1;
			// skip the phonet
			tuple += gbstrlen(tuple) + 1;
			pop = atoi(tuple);
		}
		// else get it by getphrasePopularity 
		else {
			uint64_t h = hash64d(phrase, gbstrlen(phrase));
			pop = g_speller.getPhrasePopularity(phrase, h, false, 
							    m_lang);
		}

		int32_t indx = numPhrases;
		// if not full
		if ( numPhrases < maxPhrases )
			numPhrases++;
		// if full 
		else{
			if ( minPop >= pop )
				continue;
			int32_t minIndx = 0;
			minPop = pops[0];
			for ( int32_t j = 1; j < maxPhrases; j++ ){
				if ( minPop < pops[j] )
						continue;
				minPop = pops[j];
				minIndx = j;
			}
			if ( minPop >= pop )
				continue;
			indx = minIndx;
			minPop = pop;
		}
		
		// store the pop
		pops[indx] = pop;
		strcpy ( &phrases[MAX_FRAG_SIZE * indx],phrase );
		log (LOG_DEBUG,"speller: Narrow phrase=%s, pop=%"INT32"",
		     &phrases[MAX_FRAG_SIZE * indx], pops[indx]);
	}

	uint64_t took = gettimeofdayInMilliseconds() - start;
	if ( took > 5)
		log ( LOG_WARN,"lang: Finding narrow phrases took %"INT64" ms",
		      took );
	return numPhrases;
}
*/

// . return the clean buffer that can be spellchecked
// . in utf8 always now
bool Language::makeClean( char *src, int32_t srcSize,
			  char *dst, int32_t dstSize ) {
	//char *pin = inBuf;
	//char *pout = outBuf;
	char *srcEnd = src + srcSize;
	char *dstEnd = dst + dstSize;
	char cs;

	//while ( pout - outBuf < outBufSize && *pin != '\0' ){
	for ( ; src < srcEnd ; src += cs ) {

		cs = getUtf8CharSize ( src );
		//UChar32 c = 0;
		//if ( isUTF16 )
		//	c = utf16Decode( (UChar *)pin, &(UChar *)pin );
		//else
		//	c = utf8Decode ( pin, &pin );
		// Since we're english cannot check anything but ASCII
		//if ( c > 0x7f )
		//	return false;
		//if (!ucIsAlnum(c) && !ucIsWhiteSpace(c) && c != (int32_t)'\'' &&
		//     c != (int32_t)' ' && c != (int32_t)'-' )
		//	return false;

		// skip more advanced forms of punct
		if ( ! is_alnum_utf8  ( src ) &&
		     ! is_wspace_utf8 ( src ) &&
		     *src != '\'' &&
		     *src != ' '  &&
		     *src != '-' )
			return false;

		// return false to avoid overflow
		if ( dst + 5 >= dstEnd ) return false;

		if ( cs == 1 ) *dst++ = to_upper_a (*src);
		else            dst  += to_upper_utf8 ( dst , src );

		// write the char as upper case
		//dst += getClean ( dst , src );

	}
	// null end it
	*dst = '\0';
	return true;
}

// returns the number of recommendations that were found
// First finds recommendations by the soundslike (phonetic) score
// Then tries to split the word and finds recommendations by the word score
// Stores the top MAX_RECOMMENDATIONS in the array, and then returns the
// highest popularity recommendation out of them
bool Language::getRecommendation( char *origWord, int32_t origWordLen,
				  char *recommendation, int32_t recommendationLen,
				  bool *found, int32_t *score, int32_t *popularity, 
				  bool  forceReco ){

	// if rules and words are not loaded, return
	if ( m_numRules == 0 || m_numTuples == 0 )
		return true;

	// don't check for recommendation if the original phrase is more than
	// MAX_PHRASE_LEN - 1
	if ( origWordLen >  MAX_PHRASE_LEN - 1 ) 
		return false;
	char origPhonet[MAX_PHRASE_LEN];
	char origClean[MAX_PHRASE_LEN];
	char possiblePhonet[ MAX_PHRASE_LEN ];

	Reco recos[MAX_RECOMMENDATIONS];
	// also keep the lowest score that we've found.
	int32_t lowestScore = LARGE_SCORE;

	/*char recos[MAX_RECOMMENDATIONS][MAX_PHRASE_LEN];
	  int32_t recoScores[MAX_RECOMMENDATIONS];*/
	int32_t numRecos = 0;
	// null end recommendation in case we don't find anything.
	*recommendation = '\0';

	*found = false;
	*score = LARGE_SCORE;
	*popularity   = 0;

	// no recommendations for 1 letter words
	if ( origWordLen < 2 )
		return false;
	
	// no recommendation if the word is found in the dictionary
	if ( !forceReco ){
		// if we are spell checking a query then we start with the
		// phrases and then move on to individual words. This should
		// eliminate bugs like saying "brittany spears" is correct
		// because the phrase shall be checked before individual words
		uint64_t h = hash64d( origWord, gbstrlen(origWord));
		if ( g_speller.getPhrasePopularity( origWord, 
							h, false ) != 0 ){
			*found = true;
			return false;
		}
		
		// check if it is present in the distributed dictionary
		if ( m_distributedPopPhrases.getSlot ( h ) != -1 ){
			*found = true;
			return false;
		}
	}

	//int32_t minRecoScore = LARGE_SCORE;

	// clean the word, i.e. convert word to uppercase and
	// remove possible accents
	if ( !makeClean ( origWord, origWordLen, origClean, MAX_PHRASE_LEN) )
		return false;
	
	//	memset ( phonet, '\0', MAX_PHRASE_LEN );

	// get the phonetic
	getPhonetic ( origClean, gbstrlen(origClean), origPhonet, 
		      MAX_PHRASE_LEN );

	log ( LOG_DEBUG,"speller: original - %s %s %s",origWord,
	      origClean, origPhonet );

	// this is the max score that we are trying to get
	// this is the radius around the misspelled word that we are checking
	int32_t tryForScore = 3 * ( m_wordWeight * m_editDistanceWeightsMax )/100;
	// decrease score by 50pc if the length of the phonet is less than 5
	// decrease score by 20pc if the length of the phonet is less than 7
	if ( gbstrlen(origPhonet) < 5 ) tryForScore -= tryForScore / 2;
	else if ( gbstrlen(origPhonet) < 7 ) tryForScore -= tryForScore / 5;
	
	

	// first try the same phonetic as the original word
	int32_t origLen = gbstrlen(origPhonet);

	// first add the original
	strcpy ( possiblePhonet, origPhonet );

	// get recos from this phonet
	numRecos = tryPhonet( possiblePhonet, origPhonet, 
			      origClean, tryForScore,
			      recos, numRecos, &lowestScore );

	// generate different phonets using addition, deletion, substitution
	// and swapping.
	// ADDITION
	for ( int32_t i = 0; i < origLen + 1; i++ ){
		for ( int32_t j = 0; j < MAX_CHARS; j++ ){
			if ( !m_ruleChars[j] ) continue;
			char *p = possiblePhonet;
			// first put in all the chars the are before the char
			// to be added
			gbmemcpy ( p, origPhonet, i ); p += i;
			// the index of m_ruleChars[] is the char to be added
			*p++ = j;
			gbmemcpy ( p, origPhonet + i, origLen - i ); 
			p += origLen - i;
			*p++ = '\0';
			numRecos = tryPhonet( possiblePhonet, origPhonet,
					      origClean, tryForScore,
					      recos, numRecos, &lowestScore );
		}
	}
	
	// DELETION
	for ( int32_t i = 0; i < origLen; i++ ){
		char *p = possiblePhonet;
		// put the chars that come before the deleted char
		gbmemcpy ( p, origPhonet, i ); p += i;
		// put the chars that come after the deleted char
		gbmemcpy ( p, origPhonet + i + 1, origLen - i - 1 ); 
		p += origLen - i - 1;
		*p++ = '\0';
		numRecos = tryPhonet( possiblePhonet, origPhonet,
				      origClean, tryForScore,
				      recos, numRecos, &lowestScore );
	}

	// SUBSTITUTION
	for ( int32_t i = 0; i < origLen; i++ ){
		for ( int32_t j = 0; j < MAX_CHARS; j++ ){
			if ( !m_ruleChars[j] ) continue;
			char *p = possiblePhonet;
			// cannot substitue if both chars are the same
			if ( j == *( origPhonet + i ) ) continue;
			// put the chars that come before the substituted char
			gbmemcpy ( p, origPhonet, i ); p += i;
			// substitute the char
			*p++ = j;
			// put the chars that come after the deleted char
			gbmemcpy ( p, origPhonet + i + 1, origLen - i - 1); 
			p += origLen - i - 1;
			*p++ = '\0';
			numRecos = tryPhonet( possiblePhonet, origPhonet, 
					      origClean, tryForScore,
					      recos, numRecos, &lowestScore );
		}
	}
	
	// SWAPPING
	for ( int32_t i = 0; i < origLen - 1; i++ ){
		char *p = possiblePhonet;
		// cannot swap if both chars are the same
		if ( *( origPhonet + i ) == *( origPhonet + i + 1 ) ) continue;
		// put the chars that come before the swapped char
		gbmemcpy ( p, origPhonet, i ); p += i;
		//swap the chars
		*p++ = *( origPhonet + i + 1);
		*p++ = *( origPhonet + i );
		// put the chars that come after the deleted char
		gbmemcpy ( p, origPhonet + i + 2, origLen - i - 2); 
		p += origLen - i - 2;
		*p++ = '\0';
		numRecos = tryPhonet( possiblePhonet, origPhonet,
				      origClean, tryForScore, 
				      recos, numRecos, &lowestScore );
	}

	// check if splitting the word gives us any good recommendations
	// this works like the try_split() function of aspell in suggest.cpp

	// dont split the word if its less than 4 chars
	if ( gbstrlen(origWord) < 4 ) 
		goto skipSplit;

	// copy it over to another string
	char splitWord[MAX_PHRASE_LEN];
	strcpy ( splitWord, origWord );

	splitWord[ gbstrlen(splitWord) + 1 ] = '\0';
	splitWord[ gbstrlen(splitWord) ] = splitWord[ gbstrlen(splitWord) - 1 ];
	
	for ( int32_t i = gbstrlen( origWord ) - 2; i >= 2; --i) {
		splitWord[i+1] = splitWord[i];
		splitWord[i] = '\0';
		
		uint64_t h = hash64d ( splitWord, gbstrlen(splitWord));
		// check if the split words exist in the dictionary
		int32_t pop = g_speller.getPhrasePopularity(splitWord,h,false);
		if ( pop == 0 ){
			// check the distributed dict also
			int32_t slot = m_distributedPopPhrases.getSlot(h);
			if ( slot != -1 ) 
				pop = m_distributedPopPhrases.
					getValueFromSlot(slot);
			if ( pop == 0 )
				continue;
		}
		
		h = hash64d ( splitWord + i + 1, gbstrlen(splitWord + i + 1));
		pop = g_speller.getPhrasePopularity( splitWord + i + 1, h,
						     false );
		if ( pop == 0 ){
			// check the distributed dict also
			int32_t slot = m_distributedPopPhrases.getSlot(h);
			if ( slot != -1 ) 
				pop = m_distributedPopPhrases.
					getValueFromSlot(slot);
			if ( pop == 0 )
				continue;
		}
		
		// replace the '\0' in between the split with a ' '
		splitWord[i] = ' ';
		int32_t wordScore = m_editDistanceWeightsDel2 * 3 / 2;
		char phonetReco[MAX_PHRASE_LEN];
		// get phonetic
		getPhonetic ( splitWord, gbstrlen(splitWord), phonetReco,
			      MAX_PHRASE_LEN );

		int32_t soundslikeScore = editDistance ( origPhonet,
						      phonetReco );
		// the final score taking into consideration the
		// phonetic score as well as the word score
		int32_t score = weightedAverage ( soundslikeScore, wordScore );

		if ( score > tryForScore + m_span )
			continue;
			
		// also continue if the score is greater than 2*lowestScore,
		// because then this reco doesn't have a chance
		if ( score > lowestScore * 2 )
			continue;

		// change the lowest score if needed
		if ( score < lowestScore )
			lowestScore = score;

		// try to add this to the recommendations
		/*log ( LOG_WARN, "lang: reco=%s wordScore=%"INT32" "
		      "phonetScore=%"INT32" score=%"INT32"", 
		      splitWord, wordScore, soundslikeScore, score );*/

		if ( numRecos < MAX_RECOMMENDATIONS ){
			strcpy ( recos[numRecos].reco, splitWord );
			recos[numRecos].score = score;
			numRecos++;
			continue;
		}

		int32_t maxScore = 0;
		int32_t maxIndex = 0;
		// find the largest score
		for ( int32_t k = 0; k < numRecos; k++ ){
			if ( recos[k].score > maxScore ){
				maxScore = recos[k].score;
				maxIndex = k;
			}
		}
		
		// boot out the largest score if it is more than this
		// score
		if ( score > maxScore )
			continue;
		
		strcpy ( recos[maxIndex].reco, splitWord );
		recos[maxIndex].score = score;
	}

 skipSplit:
	// if no recos return
	if ( numRecos == 0 )
		return false;

	// sort the recos according to their scores
	gbsort ( recos, numRecos, sizeof(Reco), cmpScores );

	log ( LOG_DEBUG, "speller: --------Top Recos--------" );

	// select the best recommendation among them by score
	int32_t bestRecoIndex = 0;
	int32_t bestRecoPop = -1;
	for ( int32_t i = 0; i < numRecos; i++ ){
		uint64_t h = hash64d ( recos[i].reco, 
					    gbstrlen(recos[i].reco));
		int32_t pop = g_speller.getPhrasePopularity(recos[i].reco, h, 
							 false);
		if ( pop == 0 ){
			// check the distributed dict also
			int32_t slot = m_distributedPopPhrases.getSlot(h);
			if ( slot != -1 ) 
				pop = m_distributedPopPhrases.
					getValueFromSlot(slot);
		}

		if ( ( recos[i].score < ( recos[bestRecoIndex].score * 2 ) &&
		       pop > ( bestRecoPop * 4 ) ) ||
		     ( recos[i].score == recos[bestRecoIndex].score &&
		       pop > bestRecoPop ) ){
			bestRecoPop = pop;
			bestRecoIndex = i;
		}
		log ( LOG_DEBUG,"speller: %"INT32") reco=%s score=%"INT32" pop=%"INT32"",
		      i, recos[i].reco, recos[i].score, pop );
	}

	log ( LOG_DEBUG, "speller: the best reco found is %s for word %s",
	      recos[bestRecoIndex].reco, origWord );
	// put the best reco into the recommendation
	strcpy ( recommendation, recos[bestRecoIndex].reco );
	*score = recos[bestRecoIndex].score;
	*popularity = bestRecoPop;
	return true;
}

int32_t Language::tryPhonet( char *phonetTmp, char *origPhonet, 
			  char *origClean, int32_t tryForScore,
			  Reco *recos, int32_t numRecos, int32_t *lowestScore ){ 
	// go through all the phonetics and select those that have score <= 100
	uint64_t key = hash64Lower_utf8(phonetTmp);
	int32_t slot = m_phonetics.getSlot ( key );
	if ( slot == -1 )
		return numRecos;
	
	// the value is a combination of the index and the number of
	// words having the same phonet
	uint64_t value = m_phonetics.getValueFromSlot(slot);
	
	int32_t index = value >> 32;
	int32_t numWordsInPhonet = value & 0xffffffff;

	log ( LOG_DEBUG,"speller: next phonet is %s, index=%"INT32", numWords=%"INT32"",
	      phonetTmp, index, numWordsInPhonet );	

	//if ( strcmp(phonetTmp,"WST") == 0 )
	//log(LOG_WARN,"BRTNSPS");
	
	// check the score to see if this phonet is any good.
	// phonet score is 100 for phonets that do not contain all
	// the letters of the word phonet. e.g. word Phonet = "PLKN", 
	// phonet = "PLKS" phonet score is 95 for phonets that contain 
	// all letters, and 0 where the phonets are same.
	int32_t phonetScore = limit1EditDistance( phonetTmp, origPhonet );
	if ( phonetScore >= LARGE_SCORE ) 
		return numRecos;

	//log ( LOG_WARN,"lang: checking phonet %s, "
	//"numWords=%"INT32"",phonetTmp, numWordsInPhonet);
	
	// this phonet works, for all the words under this phonet,
	// get their score.
	for ( int32_t j = 0; j < numWordsInPhonet; j++ ){
		// The dict is stored as a tuple of 
		// ( original phrase, phonetic, (lang, score)... )
		char *wordReco = m_tuplePtr[j + index];
		// make the clean Reco
		char cleanReco[MAX_PHRASE_LEN];
		// sanity check, this is in the dict, so we should be able to
		// make the word into clean
		if ( !makeClean( wordReco, gbstrlen(wordReco), cleanReco,
				 MAX_PHRASE_LEN ) ){
			char *xx = NULL; *xx = 0;
		}
		// now the phonetic
		char *phonetReco = wordReco + gbstrlen(wordReco) + 1;
		// sanity check
		if ( !cleanReco[0] || !phonetReco ){
			char *xx = NULL; *xx = 0;
		}
		
		// we want the min Score, so this is init'ed to max
		int32_t wordScore = LARGE_SCORE;
		
		// init this to phonetScore
		int32_t soundslikeScore = phonetScore;
		
		//log (LOG_WARN,"lang: %s\t%s\t%s %"INT32" %"INT32"",
		//   wordReco, cleanReco, phonetReco, 
		//   wordScore, soundslikeScore);
		
		if ( wordScore >= LARGE_SCORE ){
			int32_t slScore = soundslikeScore;
			if ( slScore >= LARGE_SCORE )
				slScore = 0;
			int32_t level =  ( 100 * tryForScore - 
					m_soundslikeWeight * slScore )/
				(m_wordWeight * 
				 m_editDistanceWeightsMin);
			
			if ( level < 0 ) 
				level = 0;
			
			if ( level >= int32_t(slScore/
					   m_editDistanceWeightsMin))
				wordScore = editDistance ( origClean,
							   cleanReco,
							   level,
							   level );
		}

		if ( wordScore >= LARGE_SCORE )
			continue;

		// this is needed for split words, that are taken
		// care of after this loop
		/*if ( soundslikeScore >= LARGE_SCORE ){
		  if ( weightedAverage( 0, wordScore ) > 
		  tryForScore )
		  continue;
		  soundslikeScore = editDistance ( origPhonet, 
		  phonetReco );
		  }*/
		

		// the final score taking into consideration the
		// phonetic score as well as the word score
		int32_t score = weightedAverage ( soundslikeScore, 
					       wordScore );

		if ( score > tryForScore + m_span || score == 0)
			continue;

		// also continue if the score is greater than 2*lowestScore,
		// because then this reco doesn't have a chance
		if ( score > *lowestScore * 2 )
			continue;

		// change the lowest score if needed
		if ( score < *lowestScore )
			*lowestScore = score;

		/*int32_t reduceScore=reduceScore(origClean,cleanReco);
		if ( reduceScore > 0 )
			log ( LOG_DEBUG,"lang: reducing score request=%s, "
			      "reco=%s, score=%"INT32", reduce=%"INT32"", origClean, 
			      cleanReco, score, reduceScore );
			      score -= reduceScore;*/
		
		//log ( LOG_WARN, "lang: reco=%s phonet=%s "
		//"wordScore=%"INT32" phonetScore=%"INT32" score=%"INT32"", 
		//wordReco, phonetReco, wordScore, 
		//soundslikeScore, score );

		/*if ( minRecoScore < score )
		  continue;
			
		  // this is our best recommendation yet
		  minRecoScore = score;
		  strcpy ( recommendation, wordReco );*/
		if ( numRecos < MAX_RECOMMENDATIONS ){
			strcpy ( recos[numRecos].reco, wordReco );
			recos[numRecos].score = score;
			numRecos++;
			continue;
		}

		int32_t maxScore = 0;
		int32_t maxIndex = 0;
		// find the largest score
		for ( int32_t k = 0; k < numRecos; k++ ){
			if ( recos[k].score > maxScore ){
				maxScore = recos[k].score;
				maxIndex = k;
			}
		}
		
		// boot out the largest score if it is more than this
		// score
		if ( score > maxScore )
			continue;
		
		strcpy ( recos[maxIndex].reco, wordReco );
		recos[maxIndex].score = score;
	}
	return numRecos;
}

int32_t Language::editDistance( char *a, char *b, int32_t level, // starting level
			       int32_t limit ) { // maximum level
	// sanity check
	if ( level <= 0  || limit < level){
		char *xx = NULL; *xx = 0;
	}

	int32_t score = LARGE_SCORE;
	while (score >= LARGE_SCORE && level <= limit) {
		if (level == 2)
			score = limit2EditDistance( a, b );
		else if (level < 5)
			score = limitEditDistance( a, b, level );
		else {
			char *xx = NULL; *xx = 0;
			//score = editDistance(a,b,w);
		}
		++level;
	} 
	return score;
}

int32_t Language::weightedAverage(int32_t soundslikeScore, int32_t wordScore) {
	return ( m_wordWeight * wordScore + 
		 m_soundslikeWeight * soundslikeScore) / 100;
}

int32_t Language::limitEditDistance( char * a, char * b, 
				    int32_t limit ) {
	limit = limit * m_editDistanceWeightsMax;
	static const int size = 10;
	struct Edit {
		char * a;
		char * b;
		int score;
	};
	Edit begin[size];
	Edit * i = begin;
	//	const char * a0;
	//	const char * b0;
	int32_t score = 0;
	int32_t min = LARGE_SCORE;
    
	while (true) {
		while (*a == *b) {
			if (*a == '\0') { 
				if (score < min) min = score;
				goto FINISH;
			} 
			++a; 
			++b;
		}
		if (*a == '\0') {
			do {
				score += m_editDistanceWeightsDel2;
				if (score >= min) goto FINISH;
				++b;
			} while (*b != '\0');
			min = score;
	
		} 
		else if (*b == '\0') {
			do {
				score += m_editDistanceWeightsDel1;
				if (score >= min) 
					goto FINISH;
				++a;
			} while (*a != '\0');
			min = score;
		} 
		// if floor(score/max)=limit/max-1 then this edit is only good
		// if it makes the rest of the string match.  So check if
		// the rest of the string matches to avoid the overhead of
		// pushing it on then off the stack
		else if ( score + m_editDistanceWeightsMax <= limit ) {
			if ( limit * m_editDistanceWeightsMin <= 
			     m_editDistanceWeightsMax * 
			     ( m_editDistanceWeightsMin + score ) ) {
				// delete a character from a
				min = checkRest( a+1, b, 
						 score + 
						 m_editDistanceWeightsDel1, 
						 NULL, min );
	    
				// delete a character from b
				min = checkRest( a, b+1,
						 score + 
						 m_editDistanceWeightsDel2, 
						 NULL, min );
	    
				if (*a == *(b+1) && *b == *(a+1)) {

					// swap two characters
					min=checkRest(a+2, b+2,
						      score +
						     m_editDistanceWeightsSwap,
						      NULL, min );

				}
				// substitute one character for another which
				// is the same thing as deleting a character
				// from both a & b
				else {
					min=checkRest(a+1, b+1, 
						      score + 
						      m_editDistanceWeightsSub,
						      NULL, min );
				}
			}
			else {
				// delete a character from a
				i->a = a + 1;
				i->b = b;
				i->score = score + m_editDistanceWeightsDel1;
				++i;
	  
				// delete a character from b
				i->a = a;
				i->b = b + 1;
				i->score = score + m_editDistanceWeightsDel2;
				++i;
	    
				// If two characters can be swapped and make
				// a match  then the substitution is pointless.
				// Also, there is no need to push this on 
				// the stack as it is going to be imminently
				// removed.
				if (*a == *(b+1) && *b == *(a+1)) {
					// swap two characters
					a = a + 2;
					b = b + 2;
					score += m_editDistanceWeightsSwap;
					continue;
				} 
				// substitute one character for another 
				// which is the same thing as deleting a
				// character from both a & b
				else {
					a = a + 1;
					b = b + 1;
					score += m_editDistanceWeightsSub;
					continue;
				}
			}
		}
	FINISH:
		if (i == begin) return min;
		--i;
		a = i->a;
		b = i->b;
		score = i->score;
	}
}



int32_t Language::limit1EditDistance( char *a, char *b ){
	int32_t min = LARGE_SCORE;
	char * amax = a;
    
	while(*a == *b) { 
		if (*a == '\0') 
			return 0; //EditDist(0, a);
		++a; ++b;
	}

	if (*a == '\0') {
      
		++b;
		if (*b == '\0') 
			return m_editDistanceWeightsDel2;
		//EditDist(ws.del2, a);
		return LARGE_SCORE;
		// EditDist(LARGE_SCORE, a);      
	} 
	else if (*b == '\0') {
		++a;
		if (*a == '\0') 
			return m_editDistanceWeightsDel1;	 
		//EditDist(ws.del1, a);
		return LARGE_SCORE;
		//EditDist(LARGE_SCORE, a);
	} 
	else {
		// delete a character from a
		min = checkRest( a+1, b, m_editDistanceWeightsDel1, 
				 amax, min );
      
		// delete a character from b
		min = checkRest( a, b+1, m_editDistanceWeightsDel2, 
				 amax, min );

		if (*a == *(b+1) && *b == *(a+1)) {
			// swap two characters
			min = checkRest( a+2, b+2, m_editDistanceWeightsSwap, 
					 amax, min );
		} 
		else {
			// substitute one character for another which is the
			// same thing as deleting a character from both a & b
			min = checkRest( a+1, b+1, m_editDistanceWeightsSub, 
					 amax, min );
		}
	}
	return min;
	//EditDist(min, amax);
}



int32_t Language::limit2EditDistance( char *a, char *b ) {
	int min = LARGE_SCORE;
	char * amax = a;
    
	while(*a == *b) { 
		if (*a == '\0') 
			return 0;
		//return EditDist(0, a);
		++a; ++b;
	}

	if (*a == '\0') {
      
		++b;
		if (*b == '\0') 
			return m_editDistanceWeightsDel2;
		//return EditDist(ws.del2,a);
		++b;
		if (*b == '\0') 
			return 2 * m_editDistanceWeightsDel2;
		//return EditDist(2*ws.del2, a);
		return LARGE_SCORE;//EditDist(LARGE_SCORE, a);
	}
	else if (*b == '\0') {
		++a;
		if (*a == '\0') 
			return m_editDistanceWeightsDel1;
		//return EditDist(ws.del1, a);
		++a;
		if (*a == '\0') 
			return 2 * m_editDistanceWeightsDel1;
		//return EditDist(2*ws.del1, a);
		return LARGE_SCORE;
		//return EditDist(LARGE_SCORE, a);
	} 
	else {
		// delete a character from a
		min = check2( a+1, b, m_editDistanceWeightsDel1, amax, min );
      
		// delete a character from b
		min = check2( a, b+1, m_editDistanceWeightsDel2, amax, min );

		if (*a == *(b+1) && *b == *(a+1)) {
			// swap two characters
			min = check2( a+2, b+2, m_editDistanceWeightsSwap, 
				      amax, min );
		} 
		else {
			// substitute one character for another which is the
			// same thing as deleting a character from both a & b
			min = check2( a+1, b+1, m_editDistanceWeightsSub, 
				      amax, min );
		}
	}
	return min;
	//return EditDist(min, amax);
}


int32_t  Language::checkRest( char *a, char *b, 
			     int32_t w, char *amax, int32_t min ){
	char *a0 = a; 
	char *b0 = b;
	while(*a0 == *b0) {
		if (*a0 == '\0') {
			if (w < min) min = w;
			break;
		}
		++a0;
		++b0;
	}
	if ( amax && amax < a0) amax = a0;
	return min;
}

int32_t Language::check2( char *a, char *b, int32_t w, char *amax, int32_t min ){
	char *aa = a; 
	char *bb = b;
	while(*aa == *bb) {
		if (*aa == '\0')  {
			if (amax < aa) amax = aa;
			if (w < min) min = w;
			break;
		}
		++aa; 
		++bb;
	}
	if (*aa == '\0') {
		if (amax < aa) amax = aa;
		if (*bb == '\0') {}
		else if (*(bb+1) == '\0' && 
			 w + m_editDistanceWeightsDel2 < min) 
			min = w + m_editDistanceWeightsDel2; 
	}
	else if (*bb == '\0') {
		++aa;
		if (amax < aa) amax = aa;
		if (*aa == '\0' && 
		    w + m_editDistanceWeightsDel1 < min) 
			min = w + m_editDistanceWeightsDel1;
	} 
	else {
		min = checkRest( aa+1, bb, 
				 w + m_editDistanceWeightsDel1, amax, min );
		min = checkRest( aa, bb+1, 
				 w + m_editDistanceWeightsDel2, amax, min );
		if (*aa == *(bb+1) && *bb == *(aa+1))
			min = checkRest( aa+2, bb+2, 
					 w + m_editDistanceWeightsSwap, 
					 amax, min);
		else
			min = checkRest( aa+1, bb+1, 
					 w + m_editDistanceWeightsSub,
					 amax, min );
	}
	return min;
}

int16_t Language::editDistance( char *a0, char *b0 ){
	int32_t aSize = gbstrlen(a0) + 1;
	int32_t bSize = gbstrlen(b0) + 1;
	//	VARARRAY(int16_t, e_d, a_size * b_size);
	int16_t e[aSize * bSize];

	//	ShortMatrix e(a_size,b_size,e_d);
	
	e[0] = 0;// e(0, 0) = 0;
	for ( int32_t j = 1; j != bSize; ++j )
		e[0 + j * aSize] = e[(j-1) * aSize] + 
			m_editDistanceWeightsDel1;
	const char * a = a0 - 1;
	const char * b = b0 - 1;
	int16_t te;
	for (int32_t i = 1; i != aSize; ++i) {
		e[i] = e[i-1] + m_editDistanceWeightsDel2;
		for (int32_t j = 1; j != bSize; ++j) {
			if (a[i] == b[j]) {
				e[i + j * aSize] = e[(i-1) + (j-1) * aSize];
			} 
			else {
				e[i + j * aSize] = m_editDistanceWeightsSub + 
					e[(i-1) + (j-1) * aSize];
				if (i != 1 && j != 1 && 
				    a[i] == b[j-1] && a[i-1] == b[j]) {
					te = m_editDistanceWeightsSwap + 
						e[(i-2) + (j-2) * aSize];
					if (te < e[i + j * aSize]) 
						e[i + j * aSize] = te;
				}
	  
				te = m_editDistanceWeightsDel1 + 
					e[i-1 + j * aSize];
				if (te < e[i + j * aSize]) 
					e[i + j * aSize] = te;
				te = m_editDistanceWeightsDel2 + 
					e[i + (j-1) * aSize];
				if (te < e[i + j * aSize]) 
					e[i + j * aSize] = te;
			}
		} 
	}
	return e[(aSize - 1) + (bSize - 1) * aSize];
}

// reduces score for substitutions that are close on the key board
// eg. we want "hakt" --> "halt", but it used to give "hakt"->"hat"
// string 'a' is the mispelling, string 'b' is the recommendation
int16_t Language::reduceScore ( char *a, char *b ){
	// reduce score only for substitutions and for 1 edit hop away
	// so essentially both strings should be of the same length
	if ( gbstrlen(a) != gbstrlen(b) )
		return 0;
	int16_t reduceScore = 0;
	while ( *a && *b ){
		if ( *a == *b ){
			a++;
			b++;
			continue;
		}
		char c = to_lower_a(*a);
		char bplace = s_keyMap[to_lower_a(*b) - 'a'];
		// check for all chars around it. For eg. for the letter 
		// 'j'(16); check 'u'(6),'i'(7),'h'(15),'k'(17),'n'(25),'m'(26)
		if ( bplace - 10 >= 0 ) {
			if ( ( s_keyboard[bplace - 10] == c ) ||
			     ( s_keyboard[bplace - 9 ] == c ) )
				reduceScore += 45;
		}
		if ( bplace < 10  ) {
			if ( s_keyboard[bplace + 1] == c )
				reduceScore += 45;
		}
		if ( bplace % 10 > 0 ) {
			if ( s_keyboard[bplace - 1] == c )
				reduceScore += 45;
		}
		if ( bplace - 10 < 28 ) {
			if ( ( s_keyboard[bplace + 10] == c ) ||
			     ( s_keyboard[bplace + 9 ] == c ) )
				reduceScore += 45;
		}
		a++;
		b++;
	}
	if ( reduceScore == 45 )
		return 45;
	return 0;
}



bool Language::getPhonetic( char *origWord, int32_t origWordLen,
			    char *target, int32_t targetLen ){
	*target = '\0';
	char word[MAX_PHRASE_LEN];
	if ( !makeClean(origWord, origWordLen, word, targetLen ) )
		return false;
	int32_t wordLen = gbstrlen(word);
	int32_t i = 0;
	int32_t j = 0;
	int32_t k = 0; // number of letters found
	int32_t n = 0; // index of m_rulesPtr where the rules for the char starts
	int32_t p = 0; // priority of the rule
	int32_t z = 0;

	int32_t k0 = -333;
	int32_t n0 = -333;
	int32_t p0 = -333;
	int32_t z0 = 0;
	char c,c0;
	const char *s;
	while ( word[i] ){
		c = word[i];
		//log ( LOG_WARN,"lang: Checking Position %"INT32", word=%s "
		//     "\ttarget=%s", j, word, target );

		z0 = 0;
		
		n = m_ruleStarts[(UChar8) c];
		// while the rule exists
		if ( n >= 0 ){
			// check all rules that start with the same letter
			while ( m_rulesPtr[n] && m_rulesPtr[n][0] == (UChar8) c ){
				//log( LOG_WARN, "lang: Checking rule "
				// "No.%"INT32", \"%s\"\t--> \"%\"s", n, 
				// m_rulesPtr[n], m_rulesPtr[n+1]);
				
				/**  check whole string  **/
				k = 1;   /** number of found letters  **/
				p = 5;   /** default priority  **/
				s = m_rulesPtr[n];
				s++;     /**  important for (see below)  "*(s-1)"  **/
				
 				// while we are not at the end of the rule and
				// the next character of the word is s and
				// s is not a digit (priority) and
				// s is not (-<^$, we are on the right track
				// so keep on checking the next char's.
				while (*s != '\0'  &&  word[i+k] == *s &&
				       !isdigit (*s)  &&  
				       strchr ("(-<^$", *s) == NULL) {
					k++;
					s++;
				}
				// letters in brackets means only one of these
				// chars must fit (OR)
				// eg. rule OH(AEIOUY) means A OR E OR I....
				if (*s == '(') {
					/**  check letters in "(..)"  **/
					// isalpha makes sure that we check 
					// only letters, and letters are only
					// inside the brackets
					if ( isalpha(word[i+k] ) &&
					     strchr(s+1, word[i+k]) != NULL ) {
						k++;
						while (*s != ')')
							s++;
						s++;
					}
				}
				p0 = (int) *s;
				k0 = k;
				// The number of dashes determines how many 
				// characters from the end will not be replaced
				while (*s == '-'  &&  k > 1) {
					k--;
					s++;
				}
				// if a `<' is appended to the search string, 
				// the search for replacement rules will
				// continue  with the replacement string 
				// and not with the next character of the word.
				if (*s == '<')
					s++;
				// the priority is the digit
				if (isdigit (*s)) {
					p = *s - '0';
					s++;
				}
				// The control character `^' says that the 
				// search string only matches at the beginning
				// of words
				if (*s == '^'  &&  *(s+1) == '^')
					s++;
				
				/* FOR FOLLOWUP RULES
				   if not at the end of the rule OR
				   ( not on rule that applies only to beginning
				   of word AND 
				   ( i is 0 OR word[i-1] is not alphabet ) AND
				   ( not on rule that applies only to end of 
				   word AND i > 0 AND word[i-1] is not alphabet
				   AND word[i+k0] is not alphabet ) */
				if (*s == '\0' || 
				    ( *s == '^'  && 
				      ( i == 0 || !isalpha(word[i-1])) && 
				      (*(s+1) != '$' || 
				       (!isalpha(word[i+k0]) ))) || 
				    (*s == '$'  &&  i > 0 && 
				     isalpha(word[i-1]) && 
				     (!isalpha(word[i+k0]) ))) {
					
					/**  search for followup rules, if: **/
					/**  parms.followup and k > 1  and  NO '-' in searchstring **/
					c0 = word[i+k-1];
					n0 = m_ruleStarts[(UChar8)c0];
					
					// followup gives better results.
					if ( //parms.followup &&  
					    k > 1  &&  n0 >= 0 &&  
					    p0 != (int) '-'  &&  
					    word[i+k] != '\0' ) {
						/**  test follow-up rule for "word[i+k]"  **/
						while (m_rulesPtr[n0][0]==c0) {
							/*log (LOG_WARN,
							  "lang: "
							  "follow-up rule "
							  "No.%"INT32"....%s\t --> 
							  %s",n0,
							  m_rulesPtr[n0], 
							  m_rulesPtr[n0+1] );*/
							/**  check whole string  **/
							k0 = k;
							p0 = 5;
							s = m_rulesPtr[n0];
							s++;

							while (*s != '\0'  &&  
							       word[i+k0] == *s && 
							       !isdigit(*s) &&
							       strchr("(-<^$",*s) == NULL) {
								k0++;
								s++;
							}
							if (*s == '(') {
								/**  check letters  **/
								if ( isalpha(word[i+k0]) &&  
								     strchr (s+1, word[i+k0] ) != NULL) {
									k0++;
									while (*s != ')'  &&  *s != '\0')
										s++;
									if (*s == ')')
										s++;
								}
							}
							while (*s == '-') {
								/**  "k0" gets NOT reduced   **/
								/**  because "if (k0 == k)"  **/
								s++;
							}
							if (*s == '<')
								s++;
							if (isdigit (*s)) {
								p0 = *s - '0';
								s++;
							}

							if (*s == '\0' || 
							    /**  *s == '^' cuts  **/
							    (*s == '$' && !isalpha(word[i+k0]))) {
								if (k0 == k) {
									/**  this is just a piece of the string  **/
									//log(LOG_WARN,"lang: discarded (too int16_t)");
									n0 += 2;
									continue;
								}

								if (p0 < p) {
									/**  priority too low  **/
									//log(LOG_WARN,"lang: discarded (priority)");
									n0 += 2;
									continue;
								}
								/**  rule fits; stop search  **/
								break;
							}
							//	log(LOG_WARN,"lang: discarded");
							n0 += 2;
						} /**  End of "while (parms.rules[n0][0] == c0)"  **/
						if (p0 >= p  && m_rulesPtr[n0][0] == c0) {
							/*log(LOG_WARN,"lang: Rule No.%"INT32", %s",n, m_rulesPtr[n]);
							  log(LOG_WARN,"lang: not used because of follow-up Rule No.%"INT32", %s",
							  n0,m_rulesPtr[n0]);*/
							n += 2;
							continue;
						}
					} /** end of follow-up stuff **/
					
					/**  replace string  **/
					/*log(LOG_WARN,"lang: Using rule "
					  "No.%"INT32", %s\t --> %s", n,
					  m_rulesPtr[n],m_rulesPtr[n+1]);*/

					s = m_rulesPtr[n+1];
					p0 = ( m_rulesPtr[n][0] != '\0' &&  
					       strchr ( m_rulesPtr[n]+1,'<') != NULL) ? 1:0;
					if (p0 == 1 &&  z == 0) {
						/**  rule with '<' is used  **/
						if (j > 0  &&  *s != '\0' && 
						    (target[j-1] == c  ||  
						     target[j-1] == *s)) {
							j--;
						}
						z0 = 1;
						z = 1;
						k0 = 0;
						while (*s != '\0'  &&  word[i+k0] != '\0') {
							word[i+k0] = *s;
							k0++;
							s++;
						}
						if (k > k0){
							//strmove (&word[0]+i+k0, &word[0]+i+k);
							char *to = &word[0]+i+k0;
							char *from = &word[0]+i+k;
							while (( *to++ = *from++ ) != 0 )
								;
						}

						/**  new "actual letter"  **/
						c = word[i];
					}
					else { /** no '<' rule used **/
						i += k - 1;
						z = 0;
						while (*s != '\0'
						       &&  *(s+1) != '\0'  &&  j < wordLen) {
							if (j == 0  ||  target[j-1] != *s) {
								target[j] = *s;
								j++;
							}
							s++;
						}
						/**  new "actual letter"  **/
						c = *s;
						if (m_rulesPtr[n][0] != '\0'
						    &&  strstr (m_rulesPtr[n]+1, "^^") != NULL) {
							if (c != '\0') {
								target[j] = c;
								j++;
							}
							//strmove (&word[0], &word[0]+i+1);
							char *to = &word[0];
							char *from = &word[0]+i+1;
							while (( *to++ = *from++ ) != 0 )
								;
							i = 0;
							z0 = 1;
						}
					}
					break;
				}  /** end of follow-up stuff **/
				n += 2;
			} /**  end of while (parms.rules[n][0] == c)  **/
		} /**  end of if (n >= 0)  **/
		if (z0 == 0) {
			// collapse_result is false for english
			if (k && p0 != -333 && !p0 &&
			    //(assert(p0!=-333),!p0) &&  
			    j < wordLen &&  c != '\0' ) { //&& 
				//(!parms.collapse_result  ||  
				// j == 0  ||  target[j-1] != c))
				/**  condense only double letters  **/
				target[j] = c;
				///printf("\n setting \n");
				j++;
			}
			/*else if (p0 || !k)
			  log( LOG_WARN,"lang: no rule found; "
			  "character \"%c\" skipped",word[i] );*/

			// goto the next character of the word
			i++;
			z = 0;
			k=0;
		}
	}  /**  end of   while ((c = word[i]) != '\0')  **/
	target[j] = '\0';
	return true;
}

bool Language::hasMispelling(char *phrase, int32_t phraseLen){
	char *p = phrase;
	char *pend = p;
	while ( pend < phrase + phraseLen ){
		while ( *pend != ' ' && pend < phrase + phraseLen )
			pend++;
		char word[1024];
		gbmemcpy(word, p, pend - p);
		word[pend - p] = '\0';
		uint32_t key = hash32d(p, pend - p);
		int32_t slot = m_misp.getSlot(key);
		if ( slot != -1 ){
			log(LOG_WARN,"lang: found mispelling in %s", word);
 			return true;
		}
		pend++;
		p = pend;
	}
	return false;
}


///////////////////////////////////////////////////////
// DICTIONARY GENERATION ROUTINES BELOW HERE
//
///////////////////////////////////////////////////////

/*
// . return false and set g_errno on error, true on success
bool Language::generateDicts ( int32_t numWordsToDump , char *coll ) {
	log(LOG_INIT,
	    "lang: Reading first %"INT32" words from titledb records in "
	    "collection '%s'.",
	    numWordsToDump,coll);

	// ensure we got a dict dir in our working dir
	char dd[1024];
	if ( gbstrlen ( g_hostdb.m_dir ) > 1000 ) {
		g_errno = EBADENGINEER;
		log("lang: Working directory %s is too long.",
		    g_hostdb.m_dir);
		return false;
	}
	sprintf ( dd , "mkdir %sdict.new/" , g_hostdb.m_dir );
	log(LOG_INIT,"lang: %s",dd);
	if ( gbsystem ( dd ) == -1 ) return false;

	sprintf ( dd , "mkdir %stmp/" , g_hostdb.m_dir );
	log(LOG_INIT,"lang: %s",dd);
	if ( gbsystem ( dd ) == -1 ) return false;

	// . loop through all titleRecs
	// . put all words/phrases that begin with letter X in file
	//   words.Y, where Y is the numeric value of to_dict_char(X)
	// . don't dump out more than "100,000" words/phrases
	// . only dump out one title rec per IP
	// . do not dump out a word/phrase more than once for the same titleRec
	// . stores files in /tmp/ dir


	if (!ucInit(g_hostdb.m_dir)) 
		return log("Unicode initialization failed!");
	g_conf.m_spiderdbMaxTreeMem = 1024*1024*30;
	g_titledb.init ();
	g_collectiondb.init(true);
	g_titledb.addColl ( coll );
	
	// load the mispellings file first
	//if ( !loadMispelledWords() )
	//  log (LOG_WARN,"lang: mispelled file could not be loaded");
	
	//log(LOG_DEBUG, "lang: making query files");
	//if( !makeQueryFiles ( ) )
	//	return log("lang: had error: %s.",
	//	mstrerror(g_errno));

	log(LOG_DEBUG, "lang: making word files");
	if( ! makeWordFiles ( numWordsToDump , MAX_WORDS_PER_PHRASE , coll ) )
		return log("lang: had error: %s.",
			   mstrerror(g_errno));
	log(LOG_DEBUG, "lang: making pop files");
	if ( ! makePopFiles ( numWordsToDump , MAX_WORDS_PER_PHRASE , coll ) )
		return log("lang: had error: %s.",
			   mstrerror(g_errno));

	// add words from /usr/dict/words to the word files
	//if ( ! addDictWords ( ) ) return false;

	// sort each file
	for ( int32_t i = 0 ; i < NUM_CHARS ; i++ ) {
		char tmp[1024];
		// . sort should treat all lower chars as upper
		// . sort in reverse order so longer fragments are on top
		//   of their int16_ter sub fragments so if they have the
		//   same score in the end, we'll keep the longer fragment
		sprintf(tmp,"sort -f -r %stmp/%s/%s.words.%"INT32" > "
			"%stmp/%s/%s.words.%"INT32".sorted",
			g_hostdb.m_dir, getLanguageAbbr(m_lang),
			getLanguageAbbr(m_lang), i, g_hostdb.m_dir,
			getLanguageAbbr(m_lang), getLanguageAbbr(m_lang), i);
		log(LOG_INIT,"lang: %s",tmp);
		gbsystem ( tmp );
	}

	// . now convert each sorted file into a unique list of word/phrases 
	//   with scores
	// . score is number of times that word/phrase was found in the file
	// . truncate each file to the top "1000000" words/phrases
	if ( ! makeScoreFiles ( 180000 ))//numWordsToDump, max # words per file
		return log(
			   "lang: had error: %s.",mstrerror(g_errno));

	loadRules();

	// success
	return true;
}



// . TODO: remove bad words
// . loop through all titleRecs
// . put all words/phrases that begin with letter X in file
//   words.Y, where Y = to_dict_char(X) [that compress the char value]
// . don't dump out more than "100,000" words/phrases
// . only dump out one title rec per IP
// . do not dump out a word/phrase more than once for the same titleRec
// . stores files in /tmp/ dir
// . return false and set g_errno on error, true on success
bool Language::makeWordFiles ( int32_t numWordsToDump , int32_t numWordsPerPhrase ,
			      char *coll ) {

	int32_t numDumped = 0;

	// message
	log(LOG_INIT,"lang: Dumping first %"INT32" words/phrases.", 
	     numWordsToDump );

	// . only allow 1 vote per ip domain
	// . assume each titlerec has about 50 words in it
	uint32_t  maxNumIps   = numWordsToDump / 50 ;
	if ( maxNumIps < 100000 ) maxNumIps = 100000;
	int32_t  iptableSize = maxNumIps * 4;
	log(LOG_INIT,"lang: Allocating %"INT32" bytes.", iptableSize );
	int32_t *iptable = (int32_t *) mmalloc ( iptableSize , "Language" );
	if ( ! iptable ) {
		return log(
			   "lang: Could not allocate %"INT32" bytes: %s",
			   iptableSize,mstrerror(g_errno));
	}
	memset ( iptable , 0 , iptableSize );

	// get the default siteRec
	//SiteRec sr;
	//Url dummy;
	//dummy.set ( "www.jinx.com" , gbstrlen("www.jinx.com") );
	//sr.set (  &dummy , coll , gbstrlen(coll) , 7 ); // filenum
	// read in 12 byte key, 4 byte size then data of that size
	uint32_t ip;
	int32_t totalVoters = 0;
	uint32_t h;
	// buffer used for storing de-tagged doc content

	// JAB: warning abatement
	// int32_t xbufSize ;
	// declare up here so we can jump to done: label
	int32_t nw;
	//XmlDoc doc;
	Words w;
	Xml xml;
	Url *u;
	TitleRec tr;
	// JAB: warning abatement
	//char xbuf [ 1024*512 ] ; //1024 ];
	//int32_t jx = numWordsPerPhrase * 2; 
	// the word vote table to ensure one vote per word per doc
	int32_t  vnumEntries ;
	int32_t  vtableSize  = 0 ;
	int32_t *vtable = NULL;
	// display titlerec # we are scanning
	int32_t  count = 0;

	// open all files for appending
	int fds [ NUM_CHARS ];
	for ( int32_t i = 0 ; i < NUM_CHARS ; i++ ) {
		char ff[1024];
		sprintf ( ff , "%stmp/%s/%s.words.%"INT32"", g_hostdb.m_dir,
			  getLanguageAbbr(m_lang),getLanguageAbbr(m_lang), i );
		// delete it first
		unlink ( ff );
		// then open a new one for appending
		fds[i] = open ( ff , 
				O_CREAT | O_RDWR | O_APPEND ,
//				S_IRUSR |S_IWUSR |S_IRGRP |S_IWGRP| S_IROTH);
		if ( fds[i] < 0 )
			return log("lang: Could not open %s for writing: "
				   "%s.",ff, strerror(errno));
	}

	// message 
	//log(LOG_INIT,"lang: Scanning title recs for words and phrases in "
	//    "%s",colldir);

	// 
	// THE TITLE SCAN LOOP
	// 

	//g_conf.m_spiderdbMaxTreeMem = 1024*1024*30;
	//g_titledb.init ();
	//g_collectiondb.init(true);
	//g_titledb.addColl ( coll );
	key_t startKey ;
	key_t endKey   ;
	startKey.setMin();
	endKey.setMax();
	startKey = g_titledb.makeFirstTitleRecKey ( 0 ); // docid );
	// turn off threads
	g_threads.disableThreads();
	// get a meg at a time
	int32_t minRecSizes = 1024*1024;
	Msg5 msg5;
	Msg5 msg5b;
	RdbList list;
	key_t k       ;
	char *rec     ;
	int32_t  recSize ;
	int32_t  sameip = 0;
	int32_t  y;
	char  quality;
 loop:
	// use msg5 to get the list, should ALWAYS block since no threads
	if ( ! msg5.getList ( RDB_TITLEDB   ,
			      //"main"        , // coll          ,
			      coll          ,
			      &list         ,
			      startKey      ,
			      endKey        ,
			      minRecSizes   ,
			      false         , // includeTree   ,
			      false         , // add to cache?
			      0             , // max cache age
			      0             , // startFileNum  ,
			      1             , // numFiles      ,
			      NULL          , // state
			      NULL          , // callback
			      0             , // niceness
			      false         , // err correction?
			      NULL          , // cache key ptr
			      0             , // retry num
			      -1            , // maxRetries
			      true          , // compensate for merge
			      -1LL          , // sync point
			      &msg5b        )){
		log(LOG_LOGIC,"lang: getList did not block.");
		return false;
	}
	// all done if empty
	log(LOG_INIT, "lang: got list: %"INT32" recs", list.getNumRecs());
	if ( list.isEmpty() ) goto done;

	k       = list.getCurrentKey();
	rec     = list.getCurrentRec();
	recSize = list.getCurrentRecSize();

	startKey = *(key_t *)list.getLastKey();
	startKey += (uint32_t) 1;
	// watch out for wrap around
	if ( startKey < *(key_t *)list.getLastKey() ) goto done;

	// 
	// END SCAN LOOP
	//

	// parse out and decompress the TitleRec
tr.set ( rec , recSize , false ) ; // owndata?

	// if quality is low, skip this doc
	quality = tr.getDocQuality();
	if ( quality < 60 )
		goto loop;

	// only do your language
	if ( tr.m_language != m_lang )
		goto loop;

	// extract the url
	u = tr.getUrl();
	// get ip 
	ip = u->getIp();
	// look up in ip table
	h = ip % maxNumIps;
	y = 0;
 ipchain:
	if ( iptable[h] ) {
		// skip if already voted
		if ( iptable[h] == (int32_t)ip ) { sameip++; goto loop; }
		// chain to next bucket
		if ( ++h >= maxNumIps ) h = 0;
		if ( ++y >  (int32_t)maxNumIps ) {
			log(LOG_LOGIC,"spell: IP table is too small. "
			    "Exiting.");
			char *xx = NULL; *xx = 0;
		}
		goto ipchain;
	}
	// store in bucket so no doc from this ip votes again
	iptable[h] = ip;
	// count the voters
	totalVoters++;

	// parse all the tags out
	//doc.set ( &tr , &sr );
	// store in this xbuf w/o tags
	xml.set ( tr.getCharset(),tr.getContent() , tr.getContentLen() , 
		  false , 0, false ,
		  tr.getVersion() );
	//xml = doc.getXml();
// 	xbufSize = xml.getText ( xbuf      , 
// 				  1024*512  ,
// 				  0         , 
// 				  999999    ,
// 				  false     ,
// 				  true      ,
// 				  true      );
	// convert non-tag content into words
	w.set(&xml, true, true);
	// hash each phrase
	nw = w.getNumWords();

	// TODO: make the above a getWords(&w) routine!!
	// so it can take from titleRecs or query logs


	// . don't hash a word from this doc more than once
	// . wvtable = word vote table
	vnumEntries = (nw * numWordsPerPhrase * 130) / 100;
	vtableSize  = vnumEntries * 4;
	//log("mallocing2b %"INT32" bytes", vtableSize );
	if ( (count % 100) == 0 )
		log(LOG_INIT,"lang: Scanning document %"INT32" "
		    "(%"INT32" dup ips, %"INT32" words dumped).",
		    count,sameip,numDumped);
	count++;
	vtable = (int32_t *) mmalloc ( vtableSize , "Language" );
	if ( ! vtable ) {
		mfree ( iptable , iptableSize , "Language" );
		return log("lang: Failed to allocate %"INT32" "
			   "bytes: %s.",iptableSize,mstrerror(g_errno));
	}
	memset ( vtable , 0 , vtableSize );

	// every other word is punctuation, so step by 2
	for ( int32_t i = 0 ; i < nw ; i ++ ) {
		// skip punct. wordId is 0.
		if ( w.isPunct(i) ) continue;

		// is the ith word a stop word?
		// tmp buffer to hold word/phrase
		char  tmp[1024];
		char *tmpp = tmp;
		char *tmpend = tmp + 1024 - 3;
		char *ww    = w.getWord(i);
		int32_t  wwlen = w.getWordLen(i);
		if ( wwlen < 2 )
			continue;
		bool isStop = ::isStopWord ( ww, wwlen, w.getWordId (i));
		// BUT ok if Capitalized or number
		if ( isStop ) {
			if ( is_digit (ww[0])    ) isStop = false;
			if ( is_cap   (ww,wwlen) ) isStop = false;
			// e-mail, c file, c. s. lewis
			if ( wwlen == 1 && ww[0] != 'a' ) isStop = false;
		}
		// loop over # of words per phrase
		for ( int32_t k = 1 ; k < numWordsPerPhrase ; k++ ) {

			tmpp = tmp;

			// stop words cannot start dictionary phrases
			if ( k > 1 && isStop ) break;

			int32_t lastj = -1;

			// do not end on stop word either
			for ( int32_t j = i ; j < i + k * 2 ; j++ ) {
				// skip if overflow
				if ( j >= nw ) continue;
				// skip punct
				if ( w.isPunct(j) ) continue;
				// point to word
				char *ww    = w.getWord(j);
				int32_t  wwlen = w.getWordLen(j);
				// if no room to store word, skip it
				if ( tmpp + wwlen >= tmpend ) { 
					tmpp = tmp; break; }
				// write word into buf
				// convert to lower case so our sort works
				// they way it should
				char tx[1024];

				// n is how many bytes we wrote into "tx"
				int32_t n = to_lower_utf8(tmpp,tmpend,ww,wwlen);
				// advance it
				tmpp += n;

				// no longer convert to utf8, cuz title rec
				// is now already in utf8 by default!!
				//tmpp += latin1ToUtf8( tmpp, 
				//		      tmpend - tmpp,
				//		      tx, wwlen );

				// remember last word # we added
				lastj = j;
				// followed by space, apostrophe or hyphen
				if      ( ww[wwlen] == '-'  ) *tmpp = '-';
				else if ( ww[wwlen] == '\'' ) *tmpp = '\'';
				else                          *tmpp = ' ';
				tmpp++;
			}
			// bail if nothing to add
			if ( tmpp <= tmp ) 
				continue;
			// don't add dict phrase if last word is a stop word
			if ( k > 1 && lastj >= 0 ) {

				char      *ww    = w.getWord    ( lastj );
				int32_t       wwlen = w.getWordLen ( lastj );
				int64_t  wid   = w.getWordId  ( lastj );
				bool       isStop = ::isStopWord(ww,wwlen,wid);
				// BUT ok if Capitalized or number
				if ( isStop ) {
					if (is_digit (ww[0])   ) isStop=false;
					if (is_cap   (ww,wwlen)) isStop=false;
				}
				if ( isStop ) continue;
			}
			// point to last space
			tmpp--;
			// overwrite it, terminate with a \n
			*tmpp = '\n';
			// how long is it? does not include terminating \n
			int32_t tmplen = tmpp - tmp;
			// skip if nothing
			if ( tmplen <= 0 ) 
				continue;
			// skip word if it has binary chars in it
			if ( has_binary ( tmp , tmplen ) ) 
				continue;
			// debug
			//if ( strncasecmp ( tmp , "a zero" , 6 ) == 0 )
			//	log("shit");
			// get hash of word/phrase
			// we need to preserve distinguish between proper
			// and improper accent marks, so don't do just ascii
			// by using wh = w.getWordId(j)
			uint64_t hh = hash64Lower_utf8 (tmp,tmplen );
			// don't allow more than one vote per doc for a word
			int32_t ii = hh % vnumEntries;
		vchain:
			if ( vtable[ii] && vtable[ii] != (int32_t)hh ) {
				if ( ++ii >= vnumEntries ) ii = 0 ;
				goto vchain;
			}
			if ( vtable[ii] ) continue;
			// store it
			vtable[ii] = (int32_t)hh;

			// a new word for this doc
			// append the word out to file
			int32_t fn = to_dict_char(tmp[0]);
			// write the hash before the word
			//char tt[32];
			//sprintf ( tt , "%016"XINT64" ", hh );
			//if ( write ( fds[fn], tt , 17 ) != 17 )
			//	return log("spell: makeWordFiles: write: %s",
			//		   strerror(errno));
			char tmpx[2080];
			tmpp++;
			*tmpp = '\0';
			sprintf(tmpx,"%s", tmp);
			int32_t tmpxlen = gbstrlen(tmpx);
			
			// write out the trailing \n as well
			int32_t wn = write ( fds[fn] , tmpx , tmpxlen ) ;
			if ( wn != tmpxlen )
				return log("spell: makeWordFiles: write: %s",
					   strerror(errno));
			numDumped++;
			if ( numDumped >= numWordsToDump ) goto done;
		}
	}

	// breakout:
	// don't need the word voting table anymore
	if ( vtable ) mfree ( vtable , vtableSize , "Language");
	vtable = NULL;
	// get more titlerecs so we can hash more words/phrases
	goto loop;

 done:
	// don't need the word voting table anymore
	if ( vtable ) mfree ( vtable , vtableSize , "Language");
	vtable = NULL;
	// close all files
	for ( int32_t i = 0 ; i < NUM_CHARS ; i++ )
		close ( fds[i] );

	return true;
}

#define NUM_UNIFILES MAX_LANGUAGES

bool Language::makePopFiles ( int32_t numWordsToDump , int32_t numWordsPerPhrase ,
			     char *coll) {

	int32_t numDumped = 0;
	int32_t docCount = 0;

	// message
	log(LOG_INIT,"lang: Dumping first %"INT32" words/phrases.", 
	     numWordsToDump );

	// . only allow 1 vote per ip domain
	// . assume each titlerec has about 50 words in it
	uint32_t  maxNumIps   = numWordsToDump / 50 ;
	if ( maxNumIps < 100000 ) maxNumIps = 100000;
	int32_t  iptableSize = maxNumIps * 4;
	log(LOG_INIT,"lang: Allocating %"INT32" bytes.", iptableSize );
	int32_t *iptable = (int32_t *) mmalloc ( iptableSize , "Language" );
	if ( ! iptable ) {
		return log(
			   "lang: Could not allocate %"INT32" bytes: %s",
			   iptableSize,mstrerror(g_errno));
	}
	memset ( iptable , 0 , iptableSize );

	// get the default siteRec
	//SiteRec sr;
	//Url dummy;
	//dummy.set ( "www.jinx.com" , gbstrlen("www.jinx.com") );
	//sr.set (  &dummy , coll , gbstrlen(coll) , 7 ); // filenum
	// read in 12 byte key, 4 byte size then data of that size
	uint32_t ip;
	int32_t totalVoters = 0;
	uint32_t h;
	// buffer used for storing de-tagged doc content

	int32_t xbufSize ;
	// declare up here so we can jump to done: label
	int32_t nw;
	//XmlDoc doc;
	Words w;
	Xml xml;
	//Scores s;
	Url *u;
	TitleRec tr;
	char xbuf [ 1024*512 ] ; //1024 ];
	//int32_t jx = numWordsPerPhrase * 2; 
	// the word vote table to ensure one vote per word per doc
	int32_t  vnumEntries ;
	int32_t  vtableSize  = 0 ;
	int32_t *vtable = NULL;
	// display titlerec # we are scanning
	int32_t  count = 0;

	// open all files for appending
	int fds [ NUM_UNIFILES ];
	for ( int32_t i = 0 ; i < NUM_UNIFILES ; i++ ) {
		char ff[1024];
		sprintf ( ff , "%stmp/%s/%s.popwords.%"INT32"", g_hostdb.m_dir ,
			  getLanguageAbbr(m_lang),getLanguageAbbr(m_lang), i );
		// delete it first
		unlink ( ff );
		// then open a new one for appending
		fds[i] = open ( ff , 
				O_CREAT | O_RDWR | O_APPEND ,
//				S_IRUSR |S_IWUSR |S_IRGRP |S_IWGRP| S_IROTH);
		if ( fds[i] < 0 )
			return log("lang: Could not open %s for writing: "
				   "%s.",ff, strerror(errno));
	}

	// message 
	//log(LOG_INIT,"lang: Scanning title recs for words and phrases in "
	//    "%s",colldir);

	// 
	// THE TITLE SCAN LOOP
	// 

	//g_conf.m_spiderdbMaxTreeMem = 1024*1024*30;
	//g_titledb.init ();
	//g_collectiondb.init(true);
	//g_titledb.addColl ( coll );
	key_t startKey ;
	key_t endKey   ;
	startKey.setMin();
	endKey.setMax();
	startKey = g_titledb.makeFirstTitleRecKey ( 0 ); // docid );
	// turn off threads
	g_threads.disableThreads();
	// get a meg at a time
	int32_t minRecSizes = 1024*1024;
	Msg5 msg5;
	Msg5 msg5b;
	RdbList list;
	key_t k       ;
	char *rec     ;
	int32_t  recSize ;
	int32_t  sameip = 0;
	int32_t  y;
	char  quality;
	int32_t badFlags = SEC_SCRIPT|SEC_STYLE|SEC_SELECT;
	Sections ss;

 loop:
	// use msg5 to get the list, should ALWAYS block since no threads
	if ( ! msg5.getList ( RDB_TITLEDB   ,
			      //"main"        , // coll          ,
			      coll          ,
			      &list         ,
			      startKey      ,
			      endKey        ,
			      minRecSizes   ,
			      false         , // includeTree   ,
			      false         , // add to cache?
			      0             , // max cache age
			      0             , // startFileNum  ,
			      -1             , // numFiles      ,
			      NULL          , // state
			      NULL          , // callback
			      0             , // niceness
			      false         , // err correction?
			      NULL          , // cache key ptr
			      0             , // retry num
			      -1            , // maxRetries
			      true          , // compensate for merge
			      -1LL          , // sync point
			      &msg5b        )){
		log(LOG_LOGIC,"lang: getList did not block.");
		return false;
	}
	// all done if empty
	log(LOG_INIT, "lang: got list: %"INT32" recs", list.getNumRecs());
	if ( list.isEmpty() ) goto done;
	list.resetListPtr();
docloop:
	k       = list.getCurrentKey();
	rec     = list.getCurrentRec();
	recSize = list.getCurrentRecSize();


	// 
	// END SCAN LOOP
	//
	docCount++;
	// parse out and decompress the TitleRec
tr.set ( rec , recSize , false ) ; // owndata?
	// if quality is low, skip this doc
	quality = tr.getDocQuality();
	if ( quality < 60 )
		goto docdone;

	if ( tr.m_language != m_lang )
		goto docdone;

	// extract the url
	u = tr.getUrl();
	// get ip 
	ip = u->getIp();
	// look up in ip table
	h = ip % maxNumIps;
	y = 0;
 ipchain:
	if ( iptable[h] ) {
		// skip if already voted
		if ( iptable[h] == (int32_t)ip ) { sameip++; goto docdone; }
		// chain to next bucket
		if ( ++h >= maxNumIps ) h = 0;
		if ( ++y >  (int32_t)maxNumIps ) {
			log(LOG_LOGIC,"spell: IP table is too small. "
			    "Exiting.");
			char *xx = NULL; *xx = 0;
		}
		goto ipchain;
	}
	// store in bucket so no doc from this ip votes again
	iptable[h] = ip;
	// count the voters
	totalVoters++;

	// parse all the tags out
	//doc.set ( &tr , &sr );
	// store in this xbuf w/o tags
	xml.set ( tr.getCharset(),tr.getContent() , tr.getContentLen() , 
		  false , 0, false ,
		  tr.getVersion() );
	//xml = doc.getXml();
	xbufSize = xml.getText ( xbuf      , 
				  1024*512  ,
				  0         , 
				  999999    ,
				  false     ,
				  true      ,
				  true      );
	// convert non-tag content into words
	//w.set ( true, (char*)xbuf , xbufSize );
	w.set ( &xml, true, true);
	//s.set ( &w, &xml , TITLEREC_CURRENT_VERSION );
	//s.set ( &w, TITLEREC_CURRENT_VERSION , false );
	ss.set ( &w,NULL,0,NULL,0,NULL,NULL,&tr,NULL,0);
	// hash each phrase
	nw = w.getNumWords();

	// TODO: make the above a getWords(&w) routine!!
	// so it can take from titleRecs or query logs


	// . don't hash a word from this doc more than once
	// . wvtable = word vote table
	vnumEntries = (nw * numWordsPerPhrase * 130) / 100;
	vtableSize  = vnumEntries * 4;
	//log("mallocing2b %"INT32" bytes", vtableSize );
	if ( (count % 100) == 0 )
		log(LOG_INIT,"lang: Scanning document %"INT32" "
		    "(%"INT32" dup ips, %"INT32" words dumped).",
		    count,sameip,numDumped);
	count++;
	vtable = (int32_t *) mmalloc ( vtableSize , "Language" );
	if ( ! vtable ) {
		mfree ( iptable , iptableSize , "Language" );
		return log("lang: Failed to allocate %"INT32" "
			   "bytes: %s.",iptableSize,mstrerror(g_errno));
	}
	memset ( vtable , 0 , vtableSize );

	// every other word is punctuation, so step by 2
	//log("Adding %d words", nw);
	for ( int32_t i = 0 ; i < nw ; i ++ ) {
		// skip punct
		//if ( w.isPunct(i) ) continue;
		//if ( !s.getScore(i) ) continue;
		if ( ss.m_sectionPtrs[i]->m_flags & badFlags ) continue;

		// is the ith word a stop word?
		// tmp buffer to hold word/phrase
		char   tmp[2048];
		char  *tmpp = tmp;
		char  *tmpend = tmp + 2048 - 3;
		char  *ww    = w.getWord(i);
		int32_t   wwlen = w.getWordLen(i);
		bool isStop = ::isStopWord ( ww, wwlen, w.getWordId (i));
		// BUT ok if Capitalized or number
		if ( isStop ) {
			if ( w.isNum(i) ) isStop = false;
			if ( w.isUpper(i)) isStop = false;
			// e-mail, c file, c. s. lewis
			if ( wwlen == 1 && ww[0] != 'a' ) 
				isStop = false;
		}
		// loop over # of words per phrase
		for ( int32_t k = 1 ; k < numWordsPerPhrase ; k++ ) {

			tmpp = tmp;

			// stop words cannot start dictionary phrases
			if ( k > 1 && isStop ) break;

			int32_t lastj = -1;

			// do not end on stop word either
			for ( int32_t j = i ; j < i + k * 2 ; j++ ) {
				// skip if overflow
				if ( j >= nw ) continue;
				// skip punct
				//if ( w.isPunct(i+j) ) continue;
				//if ( !s.getScore(i+j) ) continue;
				if ( ss.m_sectionPtrs[j]->m_flags &badFlags )
					continue;
				// point to word
				char *ww    = w.getWord(j);
				int32_t  wwlen = w.getWordLen(j);
				// if no room to store word, skip it
				if ( tmpp + wwlen >= tmpend ) { 
					tmpp = tmp; break; }
				// write word into buf
				// convert to lower case so our sort works
				// they way it should

				// n is how many bytes we wrote into "tx"
				int32_t n = to_lower_utf8(tmpp,tmpend,ww,wwlen);
				// advance it
				tmpp += n;

				// remember last word # we added
				lastj = j;
				// followed by space, apostrophe or hyphen
				if      ( ww[wwlen] == '-'  ) *tmpp = '-';
				else if ( ww[wwlen] == '\'' ) *tmpp = '\'';
				else                          *tmpp = ' ';
				tmpp++;
			}
			// bail if nothing to add
			if ( tmpp <= tmp ) continue;
			// don't add dict phrase if last word is a stop word
			if ( k > 1 && lastj >= 0 ) {

				char      *ww    = w.getWord    ( lastj );
				int32_t       wwlen = w.getWordLen ( lastj );
				int64_t  wid   = w.getWordId  ( lastj );
				
				isStop =::isStopWord(ww,wwlen,wid);

				// BUT ok if Capitalized or number
				if ( isStop ) {
					if ( w.isNum(lastj) ) isStop=false;
					if ( w.isUpper( lastj ) ) isStop=false;
				}
				if ( isStop ) continue;
			}
			// point to last space
			//tmpp--;
			// overwrite it, terminate with a \n
			*tmpp = '\n';
			// how long is it? does not include terminating \n
			int32_t tmplen = tmpp - tmp;
			// skip if nothing
			if ( tmplen <= 0 ) continue;
			// skip word if it has binary chars in it
			if ( has_binary ( tmp , tmplen ) ) continue;
			// debug
			//if ( strncasecmp ( tmp , "a zero" , 6 ) == 0 )
			//	log("shit");
			// get hash of word/phrase
			// we need to preserve distinguish between proper
			// and improper accent marks, so don't do just ascii
			// by using wh = w.getWordId(i+j)
			uint64_t hh = hash64Lower_utf8 (tmp,tmplen );
			// don't allow more than one vote per doc for a word
			int32_t ii = hh % vnumEntries;
		vchain:
			if ( vtable[ii] && vtable[ii] != (int32_t)hh ) {
				if ( ++ii >= vnumEntries ) ii = 0 ;
				goto vchain;
			}
			if ( vtable[ii] ) continue;
			// store it
			vtable[ii] = (int32_t)hh;

			// a new word for this doc
			// append the word out to file
			//int32_t fn = to_dict_char(tmp[0]);
			int32_t fn = tr.getLanguage();
			// write the hash before the word
			//char tt[32];
			//sprintf ( tt , "%016"XINT64" ", hh );
			//if ( write ( fds[fn], tt , 17 ) != 17 )
			//	return log("spell: makeWordFiles: write: %s",
			//		   strerror(errno));
			// write out the trailing \n as well
			int32_t wn = write ( fds[fn] , tmp , tmplen + 1) ;
			if ( wn != tmplen + 1 )
				return log("spell: makePopFiles: "
					   "write: %s",
					   strerror(errno));

			numDumped++;
			if ( numDumped >= numWordsToDump ) 
				goto done;
		}
	}
	//log(LOG_INIT, "lang: got %"INT32" docs, %"INT32" words", 
	//docCount, numDumped);

	// breakout:
	// don't need the word voting table anymore
	if ( vtable ) mfree ( vtable , vtableSize , "Language");
	vtable = NULL;
docdone:
	// get more titlerecs so we can hash more words/phrases
	list.skipCurrentRecord();
	if (!list.isExhausted()) 
		goto docloop;

	startKey = *(key_t *)list.getLastKey();
	startKey += (uint32_t) 1;
	// watch out for wrap around
	if ( startKey < *(key_t *)list.getLastKey() ) goto done;
	goto loop;

 done:
	// don't need the word voting table anymore
	log(LOG_INIT, "lang: got %"INT32" docs total", docCount);
	if ( vtable ) mfree ( vtable , vtableSize , "Language");
	vtable = NULL;
	// close all files
	for ( int32_t i = 0 ; i < NUM_UNIFILES ; i++ ) close ( fds[i] );

	return true;
}

// . now convert each sorted file into a unique list of word/phrases 
//   with scores
// . score is number of times that word/phrase was found in the file
// . truncate each file to the top "maxWordsPerFile" words/phrases
bool Language::makeScoreFiles ( int32_t maxWordsPerFile ) {

	// convert each file
	for ( int32_t i = 0 ; i < NUM_CHARS ; i++ ) {

		// open the file for reading
		char ff[1024];
		sprintf ( ff , "%stmp/%s/%s.words.%"INT32".sorted", g_hostdb.m_dir,
			  getLanguageAbbr(m_lang),getLanguageAbbr(m_lang), i );
		FILE *fdr = fopen ( ff , "r" );
		if ( ! fdr )
			return log(
				   "lang: Failed to open %s for reading: "
				   "%s.",ff, strerror(errno));

		// and one for writing out score/word pairs
		sprintf ( ff, "%stmp/%s/%s.words.%"INT32".prescored",g_hostdb.m_dir,
			  getLanguageAbbr(m_lang),getLanguageAbbr(m_lang), i );
		FILE *fdw = fopen ( ff , "w" );
		if ( ! fdw )
			return log(
				   "lang: Failed to open %s for writing: "
				   "%s.",ff, strerror(errno));

		log(LOG_INIT,"lang: Making %s.", ff );

		// ongoing score count
		int32_t score = 0;
		int32_t oldscore = 0;
		// store last word/phrase in here
		char lastw [ 1029];
		lastw[0] = '\0';
		// and its hash in here
		uint64_t lasthh = 0;
		char pbuf[1024];
		//int32_t bonus = 0;
		//bool gotit = false; // do we start w/ '*'? means in dict.
		// read in each line
		while ( fgets ( pbuf , 1024 , fdr ) ) {
			char *p = pbuf;
			// skip '*'
			//if ( *p == '*' ) { gotit = true ; p++; }
			//else               gotit = false;
			// skip lines beginning with "the " TOO COMMON
			if ( (p[0] == 't' || p[0] == 'T') &&
			     strncasecmp ( p , "the ", 4 ) == 0 )
				continue;
			// also, "and "
			if ( (p[0] == 'a' || p[0] == 'A') &&
			     strncasecmp ( p , "and ", 4 ) == 0 )
				continue;
			// and, "a "
			if ( (p[0] == 'a' || p[0] == 'A') && p[1] == ' ')
				continue;
			// don't include terminating \n in the length
			int32_t plen = gbstrlen(p) - 1;
			if ( plen <= 0 ) continue;
			// skip if too big and might have been truncated
			if ( plen >= 1000 ) continue;
			// NULL terminate it to take off ending * and/or \n
			p [plen] = '\0';
			// get the hash of this word/phrase
			uint64_t hh = hash64Lower_utf8 ( p , plen );
			//sscanf ( buf , "%"XINT64"" , &hh );
			// was it same as last? if so, tally and continue
			if ( hh == lasthh ) { 
				score++;
				//if ( gotit ) bonus = IN_DICT_BONUS;
				continue; 
			}
			// add bonus to score to get final score
			//score += bonus;
			// . otherwise, we're starting a new word
			// . print out the word before us
			if ( score >= MIN_DOCS ) {
				//if ( gotit ) // bonus ) 
				//	fprintf(fdw,"%05"INT32" *%s\n",score,lastw);
				//else
				fprintf(fdw,"%05"INT32" %s\n" ,score,lastw);
			}
			// we are now the new word
			lasthh    = hh;
			strncpy ( lastw , p , 1010 );
			//if ( gotit ) bonus = IN_DICT_BONUS;
			//else         bonus = 0;
			// give us score 1
			score = 1;
		}
		// write out the last
		// skip if too big and might have been truncated
		//score += bonus;
		if ( score >= MIN_DOCS &&  gbstrlen(lastw) < 1000)  {
			//if (gotit) fprintf (fdw,"%05"INT32" *%s\n",score,lastw );
			// else       fprintf (fdw,"%05"INT32" %s\n" ,score,lastw );
			fprintf (fdw,"%05"INT32" %s\n" ,score,lastw );
		}

		fclose ( fdr );
		fclose ( fdw );

		//
		// now remove small phrases in there just because the
		// big phrase containing them is the popular one
		//

		// open the file for reading
		sprintf ( ff, "%stmp/%s/%s.words.%"INT32".prescored",g_hostdb.m_dir,
			  getLanguageAbbr(m_lang),getLanguageAbbr(m_lang), i );
		fdr = fopen ( ff , "r" );
		if ( ! fdr )
			return log(
				   "lang: Failed to open %s for reading: "
				   "%s.",ff, strerror(errno));

		// and one for writing out score/word pairs
		sprintf ( ff , "%stmp/%s/%s.words.%"INT32".scored", g_hostdb.m_dir,
			  getLanguageAbbr(m_lang),getLanguageAbbr(m_lang), i );
		fdw = fopen ( ff , "w" );
		if ( ! fdw )
			return log(
				   "lang: Failed to open %s for writing: "
				   "%s.",ff, strerror(errno));

		lastw[0] = '\0';
		// read in each line
		while ( fgets ( pbuf , 1024 , fdr ) ) {
			char *p = pbuf;
			// don't include terminating \n in the length
			int32_t plen = gbstrlen(p) - 1;
			// NULL terminate it to take off ending * and/or \n
			p [plen] = '\0';
			// get score
			int32_t score = atoi(p);
			// advance p over score and separating space
			while ( isdigit(*p) ) p++;
			p++;
			// skip '*'
			//if ( *p == '*' ) { gotit = true ; p++; }
			//else               gotit = false;
			// debug point
			//if ( strcmp ( p , "a wide variety of topics" )==0)
			//	log("got it");
			// does the new chunk match the last one?
			int32_t n;
			for ( n = 0 ; p[n] && 
				      to_lower_a(p[n]) == 
				      to_lower_a(lastw[n]); n++ );
			// cancel match if doesn't fail on a word boundary
			if ( p[n]               ) n = 0;
			if ( is_alnum(lastw[n]) ) n = 0;
			// if match subtract score so we don't leech our
			// points from him
			if ( n > 0 ) score -= oldscore;
			// if our score is now too low, don't add ourselves
			if ( score < MIN_DOCS ) continue;
			// . save it to disk
			// . this puts the asterisk back at the end of the
			//   word for easier reading
			//if ( gotit) fprintf(fdw,"%05"INT32" %s*\n",score,p);
			//else        fprintf(fdw,"%05"INT32" %s\n" ,score,p);
			fprintf(fdw,"%05"INT32"\t%s\n" ,score,p);
			// store as last
			oldscore = score;
			strncpy ( lastw , p , 1010 );
		}
		fclose ( fdr );
		fclose ( fdw );

		// sort the score file and output to dict.%"INT32"
		char bb[1024];
		sprintf( bb,
			 "sort -f -r %stmp/%s/%s.words.%"INT32".scored | "
			 "head -%"INT32" > %sdict.new/%s/%s.dict.%"INT32"",
			 g_hostdb.m_dir, getLanguageAbbr(m_lang),
			 getLanguageAbbr(m_lang), i, maxWordsPerFile,
			 g_hostdb.m_dir, getLanguageAbbr(m_lang),
			 getLanguageAbbr(m_lang), i );
		log(LOG_INIT,"lang: %s",bb);
		gbsystem ( bb );

		// make the phonets for it too
		//sprintf(bb,"%sdict.new/dict.%"INT32"",g_hostdb.m_dir,i);
		//makePhonet ( bb );
	}
	return true;
}

// Get the queries from the http query requests and use them as phrases
bool Language::makeQueryFiles ( ) {
	char buf [1024*10];
	for ( int32_t i = 1; i < 2; i++ ){
	//fdr = fopen ( "dict/queries.mamma","r" );
		char fx[1024];
		sprintf( fx,"%sdict/queries.mamma%"INT32"",g_hostdb.m_dir, i );
		FILE *fdr = fopen ( fx,"r" );
		if ( ! fdr ) {
			return log("lang: Could not open query file for "
				   "reading: %s.",strerror(errno));
		}

		// open for writing	
		char ff[1024];
		sprintf ( ff , "%stmp/dict.queries.%"INT32"", g_hostdb.m_dir, i );
		// delete it first
		unlink ( ff );
		// then open a new one for appending
		int fdw = open ( ff , 
				 O_CREAT | O_RDWR | O_APPEND ,
//				 S_IRUSR |S_IWUSR |S_IRGRP |S_IWGRP| S_IROTH);
		if ( fdw < 0 ){
			return log("lang: Could not open for %s "
				   "writing: %s.",ff, strerror(errno));
		}

		Url u;
		Query q;
		while ( fgets ( buf , 1024 * 10, fdr ) ) {
			buf[1024 * 10 - 1] = '\0';
			// length of word(s), including the terminating \n
			int32_t wlen = gbstrlen(buf) ;
			// skip if empty
			if ( wlen <= 0 ) continue;
			buf[wlen-1]='\0';

			u.set(buf,gbstrlen(buf));

			HttpRequest r1,r2;
			bool status = r1.set ( &u ) ;
			if ( !status )
				continue;

			r2.set( r1.getRequest(), r1.getRequestLen(), NULL );

			char frag[1024];
			int32_t flen;
			char *query = r2.getString( "uip",&flen );
			gbmemcpy ( frag, query, flen );
			frag[flen++] = '\t';
			int32_t queryLen;
			query = r2.getString( "q",&queryLen );

			q.set(query, queryLen, NULL, 0, true);

			// don't use truncated queries
			if ( q.m_truncated )
				continue;
			if ( q.m_isBoolean )
				continue;

			int32_t nqw = q.m_numWords;
			for ( int32_t i = 0 ; i < nqw ; i++ ) {
				int32_t fragLen = flen;
				// get a word in the Query to start a fragment
				// with
				QueryWord *qw = &q.m_qwords[i];
				// can he start the phrase?
				bool canStart = true;

				
				if (!qw->isAlphaWord()) 
					canStart = false;

				// MDW: wtf is this?
				//UCScript script = qw->wordScript();
				//if ((script != ucScriptCommon) && 
				//    (script != ucScriptLatin))
				//	canStart = false;


				if ( qw->m_ignoreWord &&
				     qw->m_ignoreWord != IGNORE_CONNECTED &&
				     qw->m_ignoreWord != IGNORE_QUOTED ) 
					canStart = false;
				// if he can't start our fragment, 
				// just copy over to "dst"
				if ( ! canStart ) {
					continue;
				}
				bool inQuotes  = qw->m_inQuotes;
				char fieldCode = qw->m_fieldCode;
				// . get longest continual fragment that 
				// . starts with word #i. get the following
				//    words that can be in a fragment
				//   that starts with word #i start of the frag
				char *p    = qw->m_word;
				int32_t  plen = 0;
				int32_t  lastLen = 0;
				for ( ; i < nqw ; i++ ) {
					// . skip if we should
					// . keep punct, however
					QueryWord *qw = &q.m_qwords[i];
					if ( qw->m_opcode                 ) 
						break;
					if ( qw->m_inQuotes  != inQuotes  ) 
						break;
					if ( qw->m_fieldCode != fieldCode ) 
						break;
					// are we punct?
					lastLen = 0;
					if ( is_alnum_utf8 ( qw->m_word ) )
						lastLen=plen;

					// inc the ptr
					plen += qw->m_wordLen;
				}
				// revisit this i in big loop since we did not
				// include it
				i--;
				// if last thing we added was punct, roll back
				// over it
				if ( lastLen ) { plen = lastLen; i--; }

				bool lastPunct = false;

				char *pend = p + plen;
				for ( ; p < pend ; p += getUtf8CharSize(p) ) {
					//skip anything but latin-1
					//if (c > 255) continue;
					if ( getUtf8CharSize(p) != 1) continue;
					// only works on a single character
					if ( ! to_dict_char ( *p ) ) 
						continue;
					// skip back to back punct/spaces
					if ( ! is_alnum_utf8(p) && lastPunct )
						continue;
					if ( ! is_alnum_utf8(p) )
						lastPunct = true;
					else 
						lastPunct=false;
					// check for a breech
					if ( fragLen+4>=1023) {
						break;
						g_errno = EBUFTOOSMALL;
						return false; }
					// language phrases are looking
					// for latin-1
					char cs = getUtf8CharSize(p);
					if ( cs == 1 ) {
						frag[fragLen++] = *p;
						continue;
					}
					// otherwise, more than 1 byte char
					gbmemcpy(frag+fragLen,p,cs);
					fragLen += cs;
				}

				// if any part of the phrase has a mispelling,
				// discard the query
				if ( hasMispelling( &frag[flen], 
						    fragLen - flen) ){
					break;
				}
				frag[fragLen++] = '\n';
				frag[fragLen] = '\0';
		
				// write out the trailing \n as well
				int32_t wn = write ( fdw, frag, fragLen ) ;
				if ( wn != fragLen )
					return log("spell: makeWordFiles: "
						   "write: %s",
						   strerror(errno));
				// break here so that we only print one phrase
				// per query
				break;
			}
		}
		fclose (fdr);
		close (fdw);
		// each ip can only vote once for a particular query. 
		// Each ip vote counts as one popular vote
		//char cmd[2048];
		// sort, the uniquify so that each ip can have only 1 occurance
		// of each phrase. Then awk to get just the phrase.
		// Then sort again and uniquify with count and remove single
		// occurance phrases. Then sort on the count to get the most
		// common phrases on top.
		//sprintf( cmd, "sort -f %s | uniq -i | "
		//"awk -F \'\\t\' \'{print $2}\' "
		//"| sort -f | uniq -i -c -d | sort -g -r -k 1,1 "
		//"> %s.uniq.sorted", ff, ff );
		//log ( LOG_INIT,"lang: %s", cmd );
		//gbsystem(cmd);
	}
	return true;
}

// Make a list of the wikipedia titles of docs found by the query
// "site:xx.wikipedia.org", where xx is the abbr of the language.
// Store in xx.wiki
bool Language::makeWikiFiles( ) {
	// open for writing	
	char ff[1024];
	sprintf ( ff , "%sdict/%s/%s.wiki", g_hostdb.m_dir, 
		  getLanguageAbbr(m_lang), getLanguageAbbr(m_lang) );
	// delete it first
	unlink ( ff );
	// then open a new one for appending
	int fdw = open ( ff , 
		     O_CREAT | O_RDWR | O_APPEND ,
//		     S_IRUSR |S_IWUSR |S_IRGRP |S_IWGRP| S_IROTH);
	if ( fdw < 0 ){
		log("lang: Could not open for %s "
		    "writing: %s.",ff, strerror(errno));
		return true;
	}

	// make a state
	StateWik *st ;
	try { st = new (StateWik); }
	catch ( ... ) {
		g_errno = ENOMEM;
		log("Lang: new(%i): %s", sizeof(StateWik),
		    mstrerror(g_errno));
		return false;
	}
	mnew ( st , sizeof(StateWik) , "LanguageWik" );

	st->m_fdw = fdw;

	char query [MAX_QUERY_LEN];
	sprintf(query,"site:%s.wikipedia.org",getLanguageAbbr(m_lang));
	st->m_coll     = g_conf.m_defaultColl;
	st->m_collLen  = gbstrlen(st->m_coll);
	// . a boolFlag of 0 means query is not boolean
	st->m_q.set ( query, gbstrlen(query), st->m_coll, st->m_collLen,
		0 ); // boolFlag

	st->m_termId = st->m_q.getTermId(0);
	st->m_startKey = g_indexdb.makeStartKey ( st->m_termId );
	st->m_endKey   = g_indexdb.makeEndKey   ( st->m_termId );
	st->m_minRecSize = 500 * 1024;
	
	if ( !st->getIndexList(  ) )
		return false;
	return st->getSummary();
}


bool StateWik::getIndexList( ) {
	// get the rdb ptr to titledb's rdb
	//Rdb *rdb = g_indexdb.getRdb();
	// -1 means read from all files in Indexdb
	// get the title rec at or after this docId
	if ( ! m_msg0.getList ( -1 ,
				0  ,
				0  ,
				0  ,    // max cache age
				false , // add to cache?
				RDB_INDEXDB  , // rdbId of 2 = indexdb
				m_coll ,
				&m_list  ,
				m_startKey  ,
				m_endKey    ,
				m_minRecSize, // recSizes 
				//st->m_useTree   , // include tree?
				//st->m_useCache  , // include cache?
				//false     , // add to cache?
				//0         , // startFileNum
				//numFiles  , // numFiles
				this        , // state
				gotIndexListWrapper ,
				0  ) )  // niceness
		return false;
	return getSummary( );
}


void gotIndexListWrapper( void *state , RdbList *list ){
	StateWik *st = (StateWik *) state;
	list->resetListPtr();
	st->getSummary();
	return;
}

bool StateWik::getSummary( ){
	
	m_numMsg20sOutstanding = 0;
	m_numMsg20sReceived = 0;
	int32_t numLaunched = 0;
	// launch MAX_FRAG_SIZE msg20's at a time, wait for all of them
	while ( numLaunched < MAX_FRAG_SIZE && !m_list.isExhausted() ){
		int64_t docId   = m_list.getCurrentDocId () ;
		// set the summary request then get it!
		Msg20Request req;
		Query *q = &m_q;
		//int32_t nt                = q->m_numTerms;
		req.ptr_qbuf             = q->getQuery();
		req.size_qbuf            = q->getQueryLen()+1;
		req.ptr_coll             = m_coll;
		req.size_coll            = m_collLen+1;
		req.m_docId              = docId;
		req.m_numSummaryLines    = 3;
		req.m_maxCacheAge        = g_conf.m_indexdbMaxIndexListAge;
		req.m_wcache             = true; // addToCache
		req.m_state              = this;
		req.m_callback           = gotSummaryWrapper;
		req.m_niceness           = 0;
		req.m_expected           = true;
		req.m_boolFlag           = q->m_isBoolean; // 2 means auto?
		req.m_allowPunctInPhrase = true;
		req.m_showBanned         = false;
		if ( ! m_msg20s[numLaunched].getSummary ( &req ) )
			m_numMsg20sOutstanding++;
#ifdef _OLDMSG20_		
		if ( !m_msg20s[numLaunched].
		     getSummary(&m_q,
				NULL,
				NULL,
				docId,
				-1, //clusterLevel
				3,//numLinesInSummary,
				g_conf.m_indexdbMaxIndexListAge,
				1                 , //addToCache
				m_coll        ,
				m_collLen     ,
				this                ,
				gotSummaryWrapper ,
				0                 ,// niceness
				//m_sequentialTitledbLookup,
				false ,// titledb restrict?
				NULL,//m_si->m_displayMetas  ,
				0,//m_si->m_displayMetasLen  ,
				0,//bigSampleRadius          ,
				0,//bigSampleMaxLen         ,
				true,//m_si->m_isMasterAdmin ,
				true   , //requireallterms
				false    , //count links
				0,
				NULL, //url
				false, //just get link info
				false,//considerTitlesFromBody
				true,// usenewsummaries
				0,
				NULL, //link info
				NULL, //hostdb
				true,//expect 2b there?
				NULL,
				0,
				0,
				true,//getvectorrec
				false,//deduping
				true,// allowPunctinPhrase
				false,//showbanned
				false,//excludeLinkText,
				false,//hackFixWords,
				false,//hackFixPhrases,
				0,//includeCachedCopy 
				false))// justgetlinkquality
			
			m_numMsg20sOutstanding++;
#endif

		m_list.skipCurrentRecord();
		numLaunched++;
	}

	m_numMsg20sLaunched = numLaunched;
	if ( m_numMsg20sOutstanding > 0 )
		return false;
	gotSummaryWrapper( this );
	return false;
}

void gotSummaryWrapper ( void *state ){
	StateWik *st = (StateWik *) state;
	st->m_numMsg20sReceived++;
	if ( !st->m_list.isExhausted() && 
	     st->m_numMsg20sLaunched < MAX_FRAG_SIZE )
		return;
	if ( st->m_numMsg20sReceived < st->m_numMsg20sOutstanding )
		return;
	if ( !st->gotSummary( ) )
		return;
	return;
}


bool StateWik::gotSummary ( ){
	
	for ( int32_t i = 0; i < m_numMsg20sLaunched; i++ ){
		if ( m_msg20s[i].m_errno )
			continue;

		char frag[MAX_FRAG_SIZE];
		int32_t flen = 0;

		strcpy(frag, m_msg20s[i].getTitle());
		flen = gbstrlen(frag);
		//log ( LOG_WARN,"lang: Got url %s with title %s",
		//     m_msg20s[i].getUrl(),
		//    m_msg20s[i].getTitle() );

		// check for two or more consecutive puncts 
		bool lastPunct = false;
		bool skip = false;
		char *p    = frag;
		char *pend = frag + flen;
		for ( ; p < pend ; p += getUtf8CharSize(p) ) {
			if ( lastPunct && !is_alnum_utf8(p) ){
				skip = true;
				break;
			}
			if ( !is_alnum_utf8 ( p ) )
				lastPunct = true;
		}
		if ( skip ) 
			continue;

		// check if all the letters are not alphabets
		int32_t numAlphas = 0;
		// anoterh loop
		p    = frag;
		for ( ; p < pend ; p += getUtf8CharSize(p) ) {
			if ( !is_alpha_utf8 ( p ) )
			     numAlphas++;
		}
		if ( numAlphas >= flen )
			continue;

		frag[flen++] = '\n';
		frag[flen] = '\0';
			
			//log ( LOG_WARN,"lang: Got url %s with title %s",
			//   m_msg20s[i].getUrl(),frag );
			
		// write out the trailing \n as well
		int32_t wn = write ( m_fdw, frag, flen ) ;
		if ( wn != flen )
			continue;
	}

	// see if u can launch more
	if ( !m_list.isExhausted() )
		return getSummary();

	// see if the termlist is over
	if ( m_list.getListSize() >= m_minRecSize ){

		// see if u can get some more of the list.
		m_startKey = *(key_t *)m_list.getLastKey();
		m_startKey += (uint32_t) 1;
		
		// watch out for wrap around
		if ( m_startKey >= *(key_t *)m_list.getLastKey() ) 
			return getIndexList();
	}

	// close the file
	close(m_fdw);
	return true;
}

// Generates the phonetics of the words of the dictionary.
// Finds the term frequency and then put it as the popularity after adjusting
bool Language::makeDict(){
	StateDict *st ;
	try { st = new (StateDict); }
	catch ( ... ) { 
		g_errno = ENOMEM;
		log("Lang: new(%i): %s", sizeof(StateDict),
		    mstrerror(g_errno));
		return true; 
	}
	mnew ( st , sizeof(StateDict) , "StateDict" );

	m_stateDict = st;

	char ff[1024];
	sprintf(ff,"%sdict/%s/%s.wl", g_hostdb.m_dir,
		getLanguageAbbr(m_lang), getLanguageAbbr(m_lang));
	File f;
	f.set (ff);

	// open file
	if ( ! f.open ( O_RDONLY ) ) {
		log("lang: open: %s",mstrerror(g_errno)); 
		return true; 
	}
	
	// TODO : CHANGE THIS TO USE fgets
	// get file size
	int32_t fileSize = f.getFileSize() ;
	
	// store a \0 at the end
	st->m_dictBufSize = fileSize + 1;

	// make buffer to hold all
	st->m_dictBuf = (char *) mmalloc ( st->m_dictBufSize , 
					   "LanguageWordsBuf" );
	if ( ! st->m_dictBuf) {
		log("lang: mmalloc: %s",mstrerror(errno));return false;
	}

	// read em all in
	if ( ! f.read ( st->m_dictBuf , fileSize , 0 ) ) {
		log("lang: read: %s", mstrerror(g_errno));
		return true;
	}

	// change \n to \0
	st->m_numTuples = 0;
	for ( int32_t i = 0 ; i < st->m_dictBufSize ; i++ ) {
		if ( st->m_dictBuf[i] != '\n' ) continue;
		st->m_dictBuf[i] = '\0';
		st->m_numTuples++;
	}

	f.close();

	// log a msg
	log(LOG_INIT,"lang: read %"INT32" words into memory", st->m_numTuples );

	// alloc space to make them into termids
	st->m_bufSize = st->m_numTuples * ( sizeof (char*) + 
					    2 * sizeof (int64_t) );
	st->m_buf = (char *) mmalloc ( st->m_bufSize, "LanguagePtrs" );
	if ( !st->m_buf ) {
		log ( LOG_WARN,"lang: could not alloc %"INT32" bytes", 
		      st->m_bufSize );
		g_errno = ENOMEM;
		return true;
	}
	char *p = st->m_buf;
	st->m_wordsPtr = (char **) p;
	p += st->m_numTuples * sizeof(char *);
	st->m_termIds = (int64_t *)p;
	p += st->m_numTuples * sizeof(int64_t);
	st->m_termFreqs = (int64_t *)p;
	p += st->m_numTuples * sizeof(int64_t);

	char *coll    = g_conf.m_defaultColl;
	int32_t collLen  = gbstrlen(coll);
	p = st->m_dictBuf;

	for ( int32_t i = 0; i < st->m_numTuples; i++ ){
		st->m_wordsPtr[i] = p;
		p += gbstrlen(p) + 1;
		int32_t wordLen = gbstrlen(st->m_wordsPtr[i]);
		// . set query class
		// . a boolFlag of 0 means query is not boolean
		Query q;
		q.set ( st->m_wordsPtr[i], wordLen , coll , collLen , 0 );
		st->m_termIds[i] = q.getTermId(0);
		st->m_termFreqs[i] = 0;
	}

	if ( !st->m_msg37.getTermFreqs ( coll               ,
					 0                  , // maxAge
					 st->m_termIds      ,
					 st->m_numTuples    ,
					 st->m_termFreqs    ,
					 this                 ,
					 gotTermFreqsWrapper,
					 0                  , // niceness
					 false              ))// exact count?
		return false;
	gotTermFreqsWrapper(this);
	return true;
}

void gotTermFreqsWrapper(void *state){
	Language *lang = (Language *) state;
	lang->gotTermFreqs(lang->m_stateDict);
}

bool Language::gotTermFreqs( StateDict *st ){
	int fd;
	char ff[1024];
	sprintf ( ff , "%sdict/%s/%s.wl.phonet",g_hostdb.m_dir,
		  getLanguageAbbr(m_lang), getLanguageAbbr(m_lang));
	// delete it first
	unlink ( ff );
	// then open a new one for appending
	fd = open ( ff , 
		    O_CREAT | O_RDWR | O_APPEND ,
//		    S_IRUSR |S_IWUSR |S_IRGRP |S_IWGRP| S_IROTH);
	if ( fd < 0 ){
		log("lang: Could not open %s for writing: "
			   "%s.",ff, strerror(errno));
		st->m_numTuples = 0;
	}

	int64_t max = 0LL;
	for ( int32_t i = 0; i < st->m_numTuples; i++ ){
		if ( st->m_termFreqs[i] > max )
			max = st->m_termFreqs[i];
	}

	char cleanWord[MAX_PHRASE_LEN];
	char phonetic[MAX_PHRASE_LEN];
	int32_t wordLen = 0;
	char tmp[1024];
	for ( int32_t i = 0; i < st->m_numTuples; i++ ){
		wordLen = gbstrlen(st->m_wordsPtr[i]);
			     
		// clean the word, i.e. convert word to uppercase and
		// remove possible accents
		makeClean( st->m_wordsPtr[i], wordLen, 
			   cleanWord, MAX_PHRASE_LEN );
		
		getPhonetic ( cleanWord, gbstrlen(cleanWord), 
			      phonetic, MAX_PHRASE_LEN );
		
		int64_t freq = ( st->m_termFreqs[i] * 32000 ) / max ;
		sprintf(tmp,"%"INT64"\t%s\t%s\n", freq,
			st->m_wordsPtr[i], phonetic);

		uint32_t wn = write ( fd , tmp , gbstrlen(tmp) ) ;
		if ( wn != gbstrlen(tmp) ){
			log("lang: makeWordFiles: write: %s",
			    strerror(errno));
			break;
		}
	}
	close(fd);
	mfree ( st->m_dictBuf, st->m_dictBufSize,"LanguageDictBuf" );
	mfree ( st->m_buf, st->m_bufSize,"LanguageBuf");
	mdelete(st,sizeof(StateDict),"StateDict");
	delete(st);
	return true;
}

#if 0
bool Language::makeAffinities(){
	// make a state
	StateAff *st ;
	try { st = new (StateAff); }
	catch ( ... ) {
		g_errno = ENOMEM;
		log("Lang: new(%i): %s", sizeof(StateAff),
		    mstrerror(g_errno));
		return false;
	}
	mnew ( st , sizeof(StateAff) , "LanguageAffinity" );
	
	st->m_fileNum = 12;
	// blocked
	if ( !openAffinityFile(st) )
		return false;
	return st->doneAffinities(st);
}


bool StateAff::openAffinityFile( ){
	if ( m_fileNum >= NUM_CHARS )
		return true;
	// open for reading
	char ff[1024];
	sprintf ( ff , "%sdict/dict.%"INT32"", g_hostdb.m_dir, m_fileNum );
	m_fdr = fopen ( ff, "r" );
	if ( !m_fdr ) {
		log("lang: test: Could not open %s for "
		    "reading: %s.", ff,strerror(errno));
		return true;
	}

	// open for writing	
	sprintf ( ff , "%sdict.new/dict.%"INT32".aff", g_hostdb.m_dir, 
		  m_fileNum );
	// delete it first
	unlink ( ff );
	// then open a new one for appending
	m_fdw = open ( ff , O_CREAT | O_RDWR | O_APPEND ,
//			   S_IRUSR |S_IWUSR |S_IRGRP |S_IWGRP| S_IROTH);
	if ( m_fdw < 0 ){
		log("lang: Could not open for %s "
		    "writing: %s.",ff, strerror(errno));
		return true;
	}

	if ( !launchAffinity(st) ){
		return false;
	}
	m_fileNum++;
	return openAffinityFile(st);
}

bool Language::launchAffinity(StateAff *st){
	//char dst[1026];
	// go through the words in dict/words
	while ( fgets ( m_buf , MAX_FRAG_SIZE , m_fdr ) ){
		// length of word(s), including the terminating \n
		int32_t wlen = gbstrlen(m_buf) ;
		// skip if empty
		if ( wlen <= 0 )
			return launchAffinity(st);
		m_buf[wlen-1]='\0';
		// skip to the phrase. titlerec dict have space as a seperator
		char *p = m_buf;
		while ( *p != ' ' )
			p++;
		p++;

		char *coll    = g_conf.m_defaultColl;
		int32_t  collLen = gbstrlen(coll);
		// . set query class
		// . a boolFlag of 0 means query is not boolean
		int32_t numTerms = 0;
		Query *q = &m_q;
		if ( q->set ( p, gbstrlen(p), coll, collLen, 0 ) ) 
			numTerms = q->getNumTerms();

		// no use doing affinities on 1 word phrases
		if ( numTerms <= 1 ){
			char dst[1096];
			sprintf( dst, "00000\t%s\n", m_buf );
			log("%s",dst);
			uint32_t wn = write(m_fdw, dst, gbstrlen(dst));
			if ( wn != gbstrlen(dst) )
				log("lang: genTopPopFile: write: %s",
				    strerror(errno));
			continue;
		}
		
		m_msg3a.reset();
		if ( !m_msg3a.
		     getDocIds( q      ,
			        coll       , 
			        collLen    ,
				100.0        ,
				g_conf.m_indexdbMaxIndexListAge,
				true ,
				0    ,//stage0
				30,
				0 ,
				this,
				gotAffinityFreqs1Wrapper ) )
			return false;
		return gotAffinityFreqs1(st);
	}
	fclose(m_fdr);
	
	close(m_fdw);
	return true;
}

void gotAffinityFreqs1Wrapper(void *state){
	StateAff *st = (StateAff *) state;
	st->gotAffinityFreqs1(st);
	return;
}

bool StateAff::gotAffinityFreqs1( ){
	m_denominator = m_msg3a.getNumTotalHits();

	// now get the phrase hits
	char *p = m_buf;
	while ( *p != ' ' )
		p++;
	// change the space to a quote
	*p = '\"';
	//go to the end
	while ( *p != '\0' )
		p++;
	//change that to quote
	*p = '\"';
	p++;
	// null end
	*p = '\0';

	p = m_buf;
	while ( *p != '\"')
		p++;
		
	char *coll    = g_conf.m_defaultColl;
	int32_t  collLen = gbstrlen(coll);
	// . set query class
	// . a boolFlag of 0 means query is not boolean
	Query *q = &m_q;
	q->set ( p, gbstrlen(p), coll, collLen, 0 );
	
	m_msg3a.reset();
	if ( !m_msg3a.
	     getDocIds( q          ,
			coll       , 
			collLen    ,
			100.0        ,
			g_conf.m_indexdbMaxIndexListAge,
			true ,
			0     ,//stage0
			30,
			0 ,
			this ,
			gotAffinityFreqs2Wrapper ) )
		return false;
	return gotAffinityFreqs2(st);
}


void gotAffinityFreqs2Wrapper(void *state){
	StateAff *st = (StateAff *) state;
	st->gotAffinityFreqs2(st);
	return;
}


bool StateAff::gotAffinityFreqs2(StateAff *st){
	m_numerator = m_msg3a.getNumTotalHits();


	double affinity = 0;
	if ( m_denominator > 0 )
		affinity = (double)m_numerator / (double)m_denominator;
	affinity *= 10000;
	
	char dst[1096];
	sprintf( dst, "%05.0f\t%s\n", affinity, m_buf );
	log("num=%"INT64", denom=%"INT64", %s",m_numerator,m_denominator,dst);
	uint32_t wn = write ( m_fdw , dst , gbstrlen(dst) ) ;
	if ( wn != gbstrlen(dst) )
		log("lang: genTopPopFile: write: %s",strerror(errno));
	
	//blocked
	if ( !launchAffinity(st) )
		return false;
	// didn't block means the file ended
	m_fileNum++;
	if ( !openAffinityFile(st) )
		return false;
	return doneAffinities(st);
}

bool StateAff::doneAffinities(StateAff *st){
	mdelete(st,sizeof(StateAff), "StateAff");
	delete(st);
	return true;
}


#endif


///////////////////////////////////////////////////////
// DICTIONARY MANIPULATION ROUTINES BELOW HERE
//
///////////////////////////////////////////////////////

// Clean query dict file of mispelleings
// NOTE: This function shall only compare each word to see if the phrase
// is present in the most commonly mispelled words list, that is present
// in the file mispelled_words. For spellchecking, use spellcheckDict()
// NOTE: Whenever you use these functions, please check the infile, outfile
// and the text format is correct
bool Language::cleanDictFile ( ) {
	char buf [1024*10];
	char fx[1024];
	sprintf( fx,"%sdict/%s/%s.query.phonet",g_hostdb.m_dir,
		 getLanguageAbbr(m_lang),getLanguageAbbr(m_lang) );
	FILE *fdr = fopen ( fx,"r" );
	if ( ! fdr ) {
		return log("lang: Could not open query file for "
			   "reading: %s.",strerror(errno));
	}

	// open for writing	
	char ff[1024];
	sprintf ( ff , "%stmp/query.phonet.clean", g_hostdb.m_dir );
	// delete it first
	unlink ( ff );
	// then open a new one for appending
	int fdw = open ( ff , 
			 O_CREAT | O_RDWR | O_APPEND ,
//			 S_IRUSR |S_IWUSR |S_IRGRP |S_IWGRP| S_IROTH);
	if ( fdw < 0 ){
		return log("lang: Could not open for %s "
			   "writing: %s.",ff, strerror(errno));
	}

	while ( fgets ( buf , 1024 * 10, fdr ) ) {
		buf[1024 * 10 - 1] = '\0';
		// length of word(s), including the terminating \n
		int32_t wlen = gbstrlen(buf) ;
		// skip if empty
		if ( wlen <= 0 ) continue;
		//buf[wlen-1]='\0';

		char *p = buf;
		while ( *p != '\t' )
			p++;
		p++;
		char *str = p;
		while ( *p != '\t' )
			p++;
		if ( hasMispelling(str, p - str) )
			continue;
			
		// write out the trailing \n as well
		int32_t wn = write ( fdw, buf, wlen ) ;
		if ( wn != wlen )
			return log("spell: makeWordFiles: "
				   "write: %s",
				   strerror(errno));
		// break here so that we only print one phrase
		// per query
	}
	return true;
}

// opens each file and creates the (score, word, phonet) tuple and stores 
// in phonet file. Normalizes scores to a high score of 32000. Also removes
// tuples for which there are no phonets and tuples that are adult.
// The incoming file is supposed to be a tuple of (score, word)
bool Language::makePhonet( char *infile){

	loadRules();

	// create the output file
	int fdw;
	char outfile[1024];
	sprintf ( outfile , "%s.phonet", infile);
	// delete it first
	unlink ( outfile );
	// then open a new one for appending
	fdw = open ( outfile , 
		     O_CREAT | O_RDWR | O_APPEND ,
//		     S_IRUSR |S_IWUSR |S_IRGRP |S_IWGRP| S_IROTH);
	if ( fdw < 0 )
		return log("lang: Could not open %s for writing: "
			   "%s.", outfile, strerror(errno));
	
	char  buf[1024];
	int32_t  max = 0;
	// open the input file
	FILE *fdr;
	// then open 
	fdr = fopen ( infile, "r" );
	if ( !fdr )
		return log("lang: Could not open %s for writing: "
			   "%s.", outfile, strerror(errno));
	
	// this loop goes through all the tuples and finds max score
	while ( fgets ( buf , 1024 , fdr ) ) {
		int32_t wlen = gbstrlen(buf);
		if ( wlen <= 0 || wlen > MAX_PHRASE_LEN )
			continue;
		// remove the newline \n
		buf [wlen - 1] = '\0';
		char *p = buf;
		while ( *p == ' ' )
			p++;
		// first is the popularity score
		if ( atoi (p) > max )
			max = atoi(p);
	}

	// close
	fclose(fdr);
	// then open 
	fdr = fopen ( infile, "r" );
	if ( !fdr )
		return log("lang: Could not open %s for writing: "
			   "%s.", outfile, strerror(errno));

	char *scorePtr;
	char *wordPtr;
	char  cleanWord[MAX_PHRASE_LEN];
	char  phonetic[MAX_PHRASE_LEN];
	int32_t  wordLen = 0;
	char  tmp[1024];
	
	// this loop goes through all the tuples and only adds those
	// tuples into the phonetic dict that have phonets. Normalizes scores.
	while ( fgets ( buf , 1024 , fdr ) ) {
		int32_t wlen = gbstrlen(buf);
		if ( wlen <= 0 || wlen > MAX_PHRASE_LEN )
			continue;
		// remove the newline \n
		buf [wlen - 1] = '\0';
		char *p = buf;
		while ( *p == ' ' )
			p++;
		// first is the popularity score
		scorePtr = p;
		int64_t score = (int64_t ) atoi(scorePtr);
		// normalize score
		score =  ( score * 32000 )/ max;

		// skip it
		while ( *p != '\t' )
			p++;
		// null end it
		*p = '\0';
		p++;
		
		wordPtr = p;
		wordLen = gbstrlen( wordPtr );

		// make the all letters in lower case
		to_lower1(p);

		// clean the word, i.e. convert word to uppercase and
		// remove possible accents
		if (!makeClean(wordPtr, wordLen, cleanWord, MAX_PHRASE_LEN)){
			log ( "removed unclean phrase %s", p );
			continue;
		}
		if ( !getPhonetic ( cleanWord, gbstrlen(cleanWord), phonetic,
				    MAX_PHRASE_LEN ) ){
			log ( "could not get phonetic of phrase %s", p );
			continue;
		}
		if ( gbstrlen(phonetic) == 0 ){
			log ( "got 0 len phonetic of phrase %s", p );
			continue;
		}
		sprintf(tmp,"%"INT64"\t%s\t%s\n",score, wordPtr, phonetic);
		
		uint32_t wn = write ( fdw , tmp , gbstrlen(tmp) ) ;
		if ( wn != gbstrlen(tmp) )
			return log("lang: makePopPhonet: write: "
				   "%s",strerror(errno));
	}
	close(fdw);
	fclose(fdr);
	// all done
	return true;
}

bool Language::genTopPopFile ( char *infile ){
	// open the input file
	FILE *fdr;
	// then open 
	fdr = fopen ( infile, "r" );
	if ( !fdr )
		return log("lang: Could not open %s for reading: "
			   "%s.", infile, strerror(errno));

	// create the output file
	int fdw;
	char outfile[1024];
	sprintf ( outfile , "%s.top", infile );
	// delete it first
	unlink ( outfile );
	// then open a new one for appending
	fdw = open ( outfile , 
		     O_CREAT | O_RDWR | O_APPEND ,
//		     S_IRUSR |S_IWUSR |S_IRGRP |S_IWGRP| S_IROTH);
	if ( fdw < 0 )
		return log("lang: Could not open %s for writing: "
			   "%s.", outfile, strerror(errno));
	
	char  buf[1024];
	int32_t count = 0;

	// this loop goes through all the words and only adds those
	// tuples into the distributed file that belong to this host.
	while ( fgets ( buf , 1024 , fdr ) ) {
		// put the first TOP_POP_PHRASES words
		if ( count++ >= TOP_POP_PHRASES )
			break;

		int32_t wlen = gbstrlen(buf);
		if ( wlen <= 0 || wlen > MAX_PHRASE_LEN )
			continue;
		uint32_t wn = write ( fdw , buf , gbstrlen(buf) ) ;
		if ( wn != gbstrlen(buf) )
			return log("lang: genTopPopFile: write: "
				   "%s",strerror(errno));
	}
	close(fdw);
	fclose(fdr);
	return true;
}

*/

// the distributed pop file is stored as a tuple of (phrase, phonet, lang, pop)
// to comply with the unified dict
bool Language::genDistributedPopFile ( char *infile, uint32_t myHash ){
	// open the input file
	FILE *fdr;
	// then open 
	fdr = fopen ( infile, "r" );
	if ( !fdr )
		return log("lang: Could not open %s for writing: "
			   "%s.", infile, strerror(errno));

	// create the output file
	int fdw;
	char outfile[1024];
	sprintf ( outfile , "%s.%"INT32"", infile, myHash );
	// delete it first
	unlink ( outfile );
	// then open a new one for appending
	fdw = open ( outfile , 
		     O_CREAT | O_RDWR | O_APPEND ,
		     getFileCreationFlags() );
		     // S_IRUSR |S_IWUSR |S_IRGRP |S_IWGRP| S_IROTH);
	if ( fdw < 0 )
		return log("lang: Could not open %s for writing: "
			   "%s.", outfile, strerror(errno));
	
	char  buf[1024];
	
	int32_t hostsPerSplit = g_hostdb.m_numHosts / g_hostdb.m_indexSplits;
	hostsPerSplit /= g_hostdb.m_numHostsPerShard;
	int32_t count = 0;

	// this loop goes through all the words and only adds those
	// tuples into the distributed file that belong to this host.
	while ( fgets ( buf , 1024 , fdr ) ) {
		// skip the first TOP_POP_PHRASES words because they shall be
		// put in the top pop file
		if ( count++ < TOP_POP_PHRASES )
			continue;
		int32_t wlen = gbstrlen(buf);
		if ( wlen <= 0 || wlen > MAX_PHRASE_LEN )
			continue;
		// remove the newline \n
		buf [wlen - 1] = '\0';
		char *p = buf;
		char *pend = p + wlen - 1;
		// first is the popularity score
		char *score = p;
		while ( *p != '\t' && p < pend )
			p++;
		// null end the score
		*p = '\0';
		p++;
		// next is the phrase
		char *phrase = p;
		while ( *p != '\t' && p < pend )
			p++;
		p++;
		// check if we're at the phonet
		if ( p >= pend )
			continue;

		char *phonet = p;
		uint64_t phonetKey = hash64Lower_utf8(phonet);
		if ( phonetKey % hostsPerSplit != myHash )
			continue;
		char tmp[1024];
		sprintf(tmp,"%s\t%s\n", phrase, score);
		// put the \n in place of \0
		//buf [wlen-1] = '\n';
		uint32_t wn = write ( fdw , tmp , gbstrlen(tmp) ) ;
		if ( (int32_t)wn != gbstrlen(tmp) )
			return log("lang: genDistributedPop: write: "
				   "%s",strerror(errno));
	}
	close(fdw);
	fclose(fdr);
	return true;
}

// heuristic code to spellcheck the dictionary
// spellcheck each word in the pop words dictionary with forceReco on so that
// we get a recommendation. Output words that have a recommendation that has
// 4 times the popularity of the word
int32_t Language::spellcheckDict(){
	if ( !loadWikipediaWords() )
		return 0;

	char ff[1024];
	sprintf ( ff , "%sdict/%s/%s.query.phonet", g_hostdb.m_dir,
		  getLanguageAbbr(m_lang), getLanguageAbbr(m_lang));
	FILE *fd = fopen ( ff, "r" );
	if ( ! fd ) {
		log("lang: test: Could not open %s for "
		    "reading: %s.", "query.phonet",strerror(errno));
		return 0;
	}

	// create the output file
	int fdw;
	char outfile[1024];
	sprintf ( outfile , "%s.spellcheck", ff );

	// delete it first
	unlink ( outfile );
	// then open a new one for appending
	fdw = open ( outfile , 
		     O_CREAT | O_RDWR | O_APPEND ,
		     getFileCreationFlags() );
		     // S_IRUSR |S_IWUSR |S_IRGRP |S_IWGRP| S_IROTH);
	if ( fdw < 0 )
		return log("lang: Could not open %s for writing: "
			   "%s.", outfile, strerror(errno));

	HashTableT <int32_t,int32_t> kickedOutPhrases;
	kickedOutPhrases.set(256);

	int32_t notFound = 0;

	char buf[1026];
	//char dst[1026];
	// go through the words in dict/words
	while ( fgets ( buf , MAX_FRAG_SIZE , fd ) ) {
		// length of word(s), including the terminating \n
		int32_t wlen = gbstrlen(buf) ;
		// skip if empty
		if ( wlen <= 0 ) continue;
		buf[wlen-1]='\0';
		for ( int32_t j = 0; j < wlen; j++ )
			if ( buf[j] == '\t')
				buf[j] = '\0';

		char *tuple = buf;
		//skip score and go to phrase
		tuple += gbstrlen(tuple) + 1;
		char *word = tuple;

		// . make the all letters in lower case
		// . TODO: fix for utf8 words?
		to_lower1_a(word);
		

		// check for adult words
		/*if ( isAdult (word) ){
			log(LOG_WARN,"lang: kicking out adult phrase=%s",
			    word);
			continue;
			}*/
		uint64_t h = hash64d ( word, gbstrlen(word));

		bool isInWiki = false;
		// if the phrase is in wikipedia, its safe
		int32_t slot = m_wiki.getSlot(h);
		if ( slot != -1 )
			isInWiki = true;

		int32_t wordPop = g_speller.getPhrasePopularity( word, h, false );
		if ( wordPop == 0 ) {
			slot = m_distributedPopPhrases.getSlot(h);
			if ( slot != -1 ){
				wordPop = m_distributedPopPhrases.
					getValueFromSlot(slot);
			}
		}

		bool isPhrase = false;
		while ( *tuple != '\0' ){
			if ( *tuple == ' ' )
				isPhrase = true;
			tuple++;
		}
		// point back to the phrase
		tuple = word;

		char recommendation[MAX_PHRASE_LEN];
		bool found;
		int32_t score;
		int32_t pop;
		
		/*
		if ( !isPhrase && !isInWiki ){
			// just the the best narrow phrase we can find
			int32_t numNarrow = 0;
			char narrow[MAX_PHRASE_LEN];
			int32_t narrowPop;
			numNarrow = narrowPhrase ( word, narrow, 
						   &narrowPop, 1 );
			
			if ( numNarrow == 0 ){
				log (LOG_WARN,"lang: no Narrow Searches "
				     "for %s",word);
				continue;
			}
			word = narrow;
			wordPop = narrowPop;
		}
		*/

		bool reco = getRecommendation( word, gbstrlen(word),
					       recommendation, MAX_PHRASE_LEN,
					       &found, &score, &pop, 
					       true );// forceReco

		// if a kicked out phrase is the recommendation, then DON'T
		// kick out this one too, because it probably means that the
		// kicked out phrase was good. BUT should we put the kicked 
		// out phrase back ??
		if ( reco && !isInWiki ){
			int32_t h1 = hash32d ( recommendation,
					    gbstrlen(recommendation) );
			slot = m_wiki.getSlot(h1);

			// if the recommendation is in wiki, then double the
			// pop of the recommendation
			if ( slot != -1 && !isInWiki ){
				log (LOG_WARN,"lang: recommendation=%s "
				     "is in the wiki. kicks out phrase %s",
				     recommendation, buf+gbstrlen(buf)+1);
				
				pop *= 2;
			}
			slot = kickedOutPhrases.getSlot(h1);
			if ( slot != -1 ){
				log (LOG_WARN,"lang: recommendation has "
				     "already been kicked out, word=%s, "
				     "reco=%s",buf+gbstrlen(buf)+1,
				     recommendation );
				reco = false;
			}
		}

		// if it is found in wikipedia OR
		// if no reco is found (even though it is a phrase) OR
		// if phrase popularity is 4x  the recommendation popularity
		// if score is less than 99.
		if ( isInWiki || !reco || wordPop * 4 > pop || score > 99 ){
			char tmp[MAX_FRAG_SIZE];
			
			sprintf(tmp,"%s\t%s\t%s\n",buf, tuple, 
				tuple + gbstrlen(tuple) + 1);
			uint32_t wn = write ( fdw , tmp , gbstrlen(tmp) );
			if ( (int32_t)wn != gbstrlen(tmp) )
				return log("spell: spellCheckDict: write: "
					   "%s",strerror(errno));
			continue;
		}
		kickedOutPhrases.addKey(h,1);
		log ( LOG_WARN,"lang: not found=%s, reco=%s, "
		      "score=%"INT32", wordPop=%"INT32", recoPop=%"INT32"",
		      buf + gbstrlen(buf) + 1, recommendation, score, 
		      wordPop, pop );
		notFound++;

	}
	close (fdw);
	fclose(fd);
	return notFound;
}

