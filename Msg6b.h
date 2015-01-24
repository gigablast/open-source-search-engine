#ifndef _MSG6B_H_
#define _MSG6B_H_


#include "gb-include.h"
#include "Msg3a.h"
#include "Msg22.h"
#include "Query.h"

class Msg8a;
class Msg6b {
public:
	Msg6b();
	~Msg6b(); 
	void 	reset();


	bool getTitlerecSample(char* query,
			       char* coll,
			       int32_t  collLen,
			       int32_t  numSamples,
			       int32_t  numToKeep,
			       void  *state,
			       bool  (*recordCallback) (void *state,
							TitleRec* tr,
							class TagRec *tagRec),
			       void  (*endCallback) (void *state),
			       bool  doSiteClustering,
			       bool  doIpClustering,
			       bool  getTagRecs,
			       int32_t  niceness);
	bool gotDocIdList();
	bool callbackIfDone();
	bool getMsg22s(int32_t sampleNum);
	bool gotMsg22s(int32_t sampleNum);
	bool gotMsg8as(int32_t sampleNum);



protected:
	Msg3a    m_msg3a;
	Msg22*   m_msg22s;
	Msg8a*    m_msg8as;
	//SiteRec* m_siteRecs;
	class TagRec   *m_tagRecs;

	int32_t   m_numMsg22s;
	int32_t   m_numGotten;
	int32_t   m_numToGet;
	int32_t   m_numToKeep;
	int32_t   m_lastSlotUsed;

	int32_t m_niceness;
	int32_t m_numOutstanding;
	//bool m_getSiteRecs;
	bool m_getTagRecs;
	
	int64_t* m_docIdPtr;
	int64_t* m_lastDocIdPtr;

	Query  m_query;
	char   m_pwd [MAX_COLL_LEN];
	char   m_coll[MAX_COLL_LEN];
	int32_t   m_collLen;

	bool  (*m_recordCallback) (void *state, 
				   TitleRec* tr,
				   class TagRec  *tagRec);
	void  (*m_endCallback)    (void *state);
	void  *m_callbackState;
};

#endif
