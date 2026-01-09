// Matt Wells, copyright Dec 2008

#ifndef _WIKI_H_
#define _WIKI_H_

#include "BigFile.h"
#include "HashTableX.h"

class Wiki {

 public:

	Wiki();
	~Wiki();
	void reset();


	int32_t getNumWordsInWikiPhrase ( int32_t i , class Words *words );

	// if a phrase in a query is in a wikipedia title, then increase
	// its affWeights beyond the normal 1.0
	bool setPhraseAffinityWeights ( class Query *q , float *affWeights ,
					bool *oneTitle = NULL );
	
	// . we hit google with random queries to see what blog sites and
	//   news sites they have
	// . stores phrase in m_randPhrase
	// . returns false if blocks, true otherwise
	// . sets g_errno on error and returns true
	// . callback will be called with g_errno set on error as well
	bool getRandomPhrase ( void *state , void (* callback)(void *state) );

	bool isInWiki ( uint32_t h ) { return ( m_ht.getSlot ( &h ) >= 0 ); };
	
	void doneReadingWiki ( ) ;

	// . load from disk
	// . wikititles.txt (loads wikititles.dat if and date is newer)
	bool load();
	
	bool loadText ( int32_t size ) ;

	// . save the binary hash table to disk to make loading faster
	// . wikititles.dat
	bool saveBinary();
	
	HashTableX m_ht;
	
	char m_buf[5000];

	char m_randPhrase[512];

	BigFile m_f;

	void *m_state;
	void (* m_callback)(void *);

	int32_t m_txtSize;

	int32_t m_errno;

	char m_opened;
	FileState m_fs;
};

extern class Wiki g_wiki;

#endif
