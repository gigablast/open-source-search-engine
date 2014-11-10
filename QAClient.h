#ifndef QACLIENT_H___
#define QACLIENT_H___

//#include "HttpServer.h"
#include "Url.h"
#include "Titledb.h"

typedef void (*qatest_callback_t)(void *);
typedef void (*httpdoc_callback_t)(void *, class HttpDoc *);
#define MAX_QA_TESTS 2000

// keep track of everything involved with getting a doc by http
class HttpDoc {
public:
	HttpDoc();
	~HttpDoc();

	void reset();
	void            get(void *state, httpdoc_callback_t callback);
	Url 		m_url;
	bool 		m_done;
	int32_t 		m_errno;
	int64_t 	m_startTime;
	int64_t 	m_elapsed;

	char           *m_buf;
	bool            m_ownBuf;
	int32_t     	m_bufSize;

	char 	       *m_content;
	int32_t     	m_contentLen;
	int16_t           m_httpStatus;
	int16_t           m_charset;
	void           *m_state;
	httpdoc_callback_t m_callback;
};

class QATest {
public:
	void reset();
	// Subclasses must define these funtions
	virtual ~QATest();
	virtual void startTest() = 0;
};

class QADiffTest: public QATest {
public:
	void reset() {
		m_state = NULL;
		m_callback = NULL;
		m_doc1.reset();
		m_doc2.reset();
		m_desc[0] = '\0';
	};
	bool set(char *desc,char *u1, char *u2, void*, qatest_callback_t);
	~QADiffTest(){reset();};
	void startTest();
	void processResults();

	HttpDoc m_doc1;
	HttpDoc m_doc2;

	// should these go in base class?
	void *m_state;
	qatest_callback_t m_callback;
	char m_desc[256];
private:
	// diff m_doc1 and m_doc2 using unix diff cmd
	void fileDiff();
	// diff m_doc1 and m_doc2 using xml tag diff
	void xmlDiff();
};

class QAClient {
	
public:
	QAClient();
	~QAClient();
	bool init(char *s1=NULL, char *s2=NULL, 
		  char *parserUrls=NULL, char *queryUrls=NULL);
	bool runTests();
	void runNextTest();
private:
	Url m_server1 ;
	Url m_server2 ;
	
	QATest *m_curTest;
	// for mnew/mdelete
	int m_curTestSize;

	char **m_parserUrls;
	int32_t m_numParserUrls;

	char **m_queries;
	int32_t m_numQueries;
	
	int32_t m_numTests;
	int32_t m_nextTest;

	char  *m_pbuf;
	int32_t   m_pbufSize;
	char **m_pbufPtr;
	int32_t   m_pbufPtrSize;
	char  *m_qbuf;
	int32_t   m_qbufSize;
	char **m_qbufPtr;
	int32_t   m_qbufPtrSize;
};

#endif 
