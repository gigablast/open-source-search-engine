// Copyright Matt Wells 2009

#ifndef _TEST_H_
#define _TEST_H_

#include "Msg4.h"
#include "Spider.h"
#include "HashTableX.h"

class Test {

 public:

	Test();

	char *getTestDir ( );

	//char *m_testDir;

	bool init ( ) ;
	void reset ( ) ;

	void removeFiles();
	void initTestRun();
	bool  injectLoop();

	void stopIt();

	// is test running now?
	bool m_isRunning;

	// how many urls we added to spiderdb via msg4
	int32_t m_urlsAdded;

	int32_t m_urlsIndexed;

	//bool m_spiderLinks;
	bool m_bypassMenuElimination;

	// are still in a loop adding urls to spiderdb via msg4?
	bool m_isAdding;

	int64_t m_testStartTime;
	
	// this is set to the errno if any error encounted during the test
	int32_t m_errno ;

	// unique test id
	int32_t m_runId ;

	char *m_coll ;

	SpiderRequest m_sreq;
	Msg4 m_msg4;

	// dedup table
	HashTableX m_dt;

	bool m_testSpiderEnabledSaved ;
	bool m_testParserEnabledSaved ;

	// file of urls in the test subdir is read into this buffer
	char *m_urlBuf ;
	char *m_urlEnd ;
	char *m_urlPtr ;
};

// the global class
extern Test g_test;

#endif
