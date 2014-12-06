// Matt Wells, copyright Aug 2012

#ifndef _WIKTIONARY_H_
#define _WIKTIONARY_H_

#define WF_NOUN         0x0001
#define WF_VERB         0x0002
#define WF_PREPOSITION  0x0004
#define WF_PRONOUN      0x0008
#define WF_ADJECTIVE    0x0010
#define WF_ADVERB       0x0020
#define WF_ARTICLE      0x0040
#define WF_INTERJECTION 0x0080
#define WF_ABBREVIATION 0x0100
#define WF_INITIALISM   0x0200
#define WF_LETTER       0x0400
#define WF_MANUAL       0x0800

//#define WF_ALLPOSFLAGS (WF_NOUN|WF_VERB|WF_PREPOSITION|WF_PRONOUN|WF_ADJECTIVE|WF_ADVERB|WF_ARTICLE|WF_INTERJECTION)


#include "BigFile.h"
#include "HashTableX.h"

class Wiktionary {

 public:

	Wiktionary();
	~Wiktionary();
	void reset();


	/*
	uint8_t getLangId ( int64_t *wid ) {
		int32_t slot = m_langTable.getSlot ( wid );
		if ( slot < 0 ) return langUnknown;
		// amibguous?
		//if ( m_langTable.getNextSlot(slot,wid) >= 0 ) 
		//	return langTranslingual;
		// ok, its unique
		uint8_t *data = (uint8_t *)m_langTable.getDataFromSlot(slot);
		// langid is lower 8 bits i think
		return *data;
	};

	uint8_t getPosFlags ( int64_t *wid , uint8_t langId ) {
		int32_t slot = m_langTable.getSlot ( wid );
		if ( slot < 0 ) return 0;
		// amibguous?
		if ( m_langTable.getNextSlot(slot,wid) >= 0 ) 
			return langTranslingual;
		// ok, its unique
		uint8_t *data = (uint8_t *)m_langTable.getDataFromSlot(slot);
		// langid is lower 8 bits i think
		return *data;
	};
	*/

	// returns a line like:
	// "pt|holandesa,holandeses,holandesas\n"
	// or
	// "en|bushmeat,bushmeats\n"
	// so you can parse the word forms out and index them
	// LATER we could add the Part of Speech...
	char *getSynSet ( int64_t wid , uint8_t langId ) {
		// 0? that's bad
		if ( wid == 0LL ) { char *xx=NULL;*xx=0;}//return NULL;
		// hash it up like we did when adding to m_tmp table
		wid ^= g_hashtab[0][langId];
		//wid ^= g_hashtab[1][posFlag];
		int32_t *offPtr ;
		// . try local table first so it overrides
		// . now this will be the one and only synset so that
		//   we can fix 'wells' from mapping to 'well,better,...'
		//   in the wikitionary-buf.txt file!
		offPtr = (int32_t *)m_localTable.getValue ( &wid );
		if ( offPtr ) return m_localBuf.getBufStart() + *offPtr;
		// try wiktionary table now
		offPtr = (int32_t *)m_synTable.getValue ( &wid );
		// got one?
		if ( offPtr ) return m_synBuf.getBufStart() + *offPtr;
		// nothing!
		return NULL;
		//if ( ! offPtr ) return NULL;
		//if ( *offPtr < 0 ) { char *xx=NULL;*xx=0; }
		//return m_synBuf.getBufStart() + *offPtr;
	};

	char *getNextSynSet ( int64_t wid , uint8_t langId , char *prev ) {
		// hash it up like we did when adding to m_tmp table
		wid ^= g_hashtab[0][langId];
		int32_t slot;
		bool gotIt = false;
		// try local table BEFORE wiktionary table
		slot = m_localTable.getSlot ( &wid );
		for ( ; slot >= 0 ; slot =m_localTable.getNextSlot(slot,&wid)){
		      int32_t *offPtr=(int32_t *)m_localTable.getValueFromSlot(slot);
		      char *ptr = m_localBuf.getBufStart() + *offPtr;
		      // make sure our mysynonyms.txt table OVERRIDES
		      // the wiktionary junk, cuz we need to do that to
		      // fix bugs in wiktionary like for 'wells' we do not
		      // want mapping to 'well,better,...' so do not allow
		      // any after the synset in mysynonyms.txt!
		      if ( ptr ) return NULL;
		      if ( gotIt ) return ptr;
		      if ( ptr == prev ) gotIt = true;
		}
		//wid ^= g_hashtab[1][posFlag];
		slot = m_synTable.getSlot ( &wid );
		for ( ; slot >= 0 ; slot = m_synTable.getNextSlot(slot,&wid)){
		      int32_t *offPtr = (int32_t *)m_synTable.getValueFromSlot(slot);
		      char *ptr = m_synBuf.getBufStart() + *offPtr;
		      if ( gotIt ) return ptr;
		      if ( ptr == prev ) gotIt = true;
		}
		return NULL;
	};


	//WikiEntry *getWiktionaryEntry ( uint64_t wid ) {
	//	return m_ht.getValue ( &h ); }
	
	// . load from disk
	// . wikititles.txt (loads wikititles.dat if and date is newer)
	bool load();
	bool test();
	bool test2();

	bool generateWiktionaryTxt ();

	bool generateHashTableFromWiktionaryTxt ( int32_t fileSize );

	bool addSynsets ( char *filename ) ;

	bool integrateUnifiedDict ( );

	// save the binary hash table to disk to make loading faster
	//bool saveHashTableBinary();
	
	bool addWord ( char *word , 
		       uint8_t posFlag , 
		       uint8_t langId ,
		       char *formOf ) ;

	bool compile ( ) ;


	HashTableX m_debugMap;
	SafeBuf    m_debugBuf;

	SafeBuf     m_localBuf;
	HashTableX  m_localTable;

	HashTableX m_dedup;
	HashTableX m_tmp;

	HashTableX m_synTable;
	SafeBuf    m_synBuf;

	SafeBuf    m_langBuf;
	
	char m_buf[5000];

	BigFile m_f;

	void *m_state;
	void (* m_callback)(void *);

	int32_t m_txtSize;

	int32_t m_errno;

	char m_opened;
	FileState m_fs;
};

extern class Wiktionary g_wiktionary;

#endif
