#include "Msg6b.h"
#include "Tagdb.h"

void gotDocIdListWrapper(void* state);
void gotMsg22Wrapper(void *state);
void gotMsg8aWrapper(void *state);

#define MAX_OUTSTANDING_MSG6B 20



Msg6b::Msg6b() {
	m_msg22s   = NULL;
	m_msg8as    = NULL;
	//m_siteRecs = NULL;
	m_tagRecs  = NULL;
	m_numMsg22s = 0;
	//	reset();
}


void Msg6b::reset() {
	m_numToGet = 0;
	m_numToKeep = 0;
	m_docIdPtr = NULL;
	m_lastDocIdPtr = NULL;
	if(m_numMsg22s <= 0) return;
	if(m_msg22s) {
		delete [] (m_msg22s); 
		mdelete(m_msg22s,
			m_numMsg22s * sizeof(Msg22), 
			"Msg6bMsg22s");  
	}
	m_msg22s = NULL;

	if(m_msg8as) {
		delete [] (m_msg8as);
		mdelete( m_msg8as, m_numMsg22s * sizeof(Msg8a), 
			 "Msg6bMsg8as" );
	}
	m_msg8as  = NULL;

	//if(m_siteRecs) {
	//	delete [] (m_siteRecs);
	//	mdelete(m_siteRecs, m_numMsg22s * sizeof(SiteRec), 
	//		"Msg6bSiteRecs" );
	//}
	//m_siteRecs = NULL;
	if ( m_tagRecs ) {
		delete [] (m_tagRecs);
		mdelete ( m_tagRecs , m_numMsg22s * sizeof(TagRec) , "msg6btb");
		m_tagRecs = NULL;
	}

	m_numMsg22s = 0;
}


Msg6b::~Msg6b() { reset(); }



bool Msg6b::getTitlerecSample(char* query,
			      char* coll,
			      int32_t  collLen,
			      int32_t  numSamples,
			      int32_t  numToKeep,
			      void  *state,
			      bool  (*recordCallback) (void *state, 
						       TitleRec* tr,
						       TagRec *tagRec),
			      void  (*endCallback) (void *state),
			      bool  doSiteClustering,
			      bool  doIpClustering,
			      bool  getTagRecs,
			      int32_t  niceness) {
	//clear it out;
	reset();
	strcpy(m_coll, coll);
 	m_collLen = collLen;
	m_callbackState = state;
	m_recordCallback = recordCallback;
	m_endCallback = endCallback;

	CollectionRec* cr = g_collectiondb.getRec ( m_coll );	
	if ( ! cr ) {
		g_errno = ENOCOLLREC;
		log("admin: no collection record found ");
		return true;
	}


	m_query.set ( query , gbstrlen(query) , m_coll , m_collLen , 0 /*boolFlag*/ );

	//log(LOG_WARN, "Msg6b got %s query.",query);

	m_numToGet = numSamples;
	m_numToKeep = numToKeep;
	if(m_numToGet < m_numToKeep) m_numToGet = m_numToKeep;

	//get twice as many docids as they want to account for errors
	int32_t docsWanted = m_numToGet * 2;
	m_niceness = niceness;
	m_getTagRecs = getTagRecs;

	int32_t tierStage0 = cr->m_tierStage0;
	int32_t tierStage1 = cr->m_tierStage1;
	int32_t tierStage2 = cr->m_tierStage2;

	// set our request
	Msg39Request req;
	req.ptr_coll                    = m_coll;
	req.size_coll                   = m_collLen+1;
	req.m_docsToGet                 = docsWanted;
	req.m_niceness                  = m_niceness;
	req.m_doSiteClustering          = doSiteClustering;
	req.m_doIpClustering            = doIpClustering;
	req.m_doDupContentRemoval       = false;
	req.m_tierSize0                 = tierStage0 ;
	req.m_tierSize1                 = tierStage1 ;
	req.m_tierSize2                 = tierStage2 ;
	req.ptr_query                   = m_query.m_orig;
	req.size_query                  = m_query.m_origLen+1;
	req.m_timeout                   = 100000; // very high

	g_errno = 0;
	// . get the docIds
	// . this sets m_msg3a.m_clusterLevels[] for us
	if ( ! m_msg3a.getDocIds ( &req             ,
				   &m_query         ,
				   this             ,
				   gotDocIdListWrapper ))
		return false;

	return gotDocIdList();
}

void gotDocIdListWrapper(void* state) {
	Msg6b* THIS = (Msg6b*)state;
	if(THIS->gotDocIdList()) {
		THIS->callbackIfDone();
	}
}


bool Msg6b::gotDocIdList() {
	m_docIdPtr = m_msg3a.getDocIds();
	m_lastDocIdPtr = m_docIdPtr + m_msg3a.getNumDocIds();

	//log(LOG_WARN, "Msg6b got %"INT32" docids.",m_msg3a.getNumDocIds());
	if(m_docIdPtr == m_lastDocIdPtr) return true;
	
	
	if(m_numToKeep < MAX_OUTSTANDING_MSG6B)
		m_numToKeep = MAX_OUTSTANDING_MSG6B;

	m_numMsg22s = m_numToKeep;
	//sanity check:
	if (m_msg22s || m_msg8as || m_tagRecs ) {
		log(LOG_WARN, "admin: trying to reuse msg6b object "
		    "without freeing");
		char *xx = NULL; *xx = 0;
	}

	try { m_msg22s = new Msg22[m_numMsg22s]; }
	catch ( ... ) {
		log("admin: Msg6b could not malloc enough memory for TitleRec sample.");
		return true;
	}
	mnew(m_msg22s, sizeof(Msg22)*m_numMsg22s, "Msg6bMsg22s");

	if(m_getTagRecs ){
		try { m_msg8as = new Msg8a[m_numMsg22s]; }
		catch ( ... ) {
			log("admin: Msg6b could not malloc enough memory.");
			return true;
		}
		mnew( m_msg8as, m_numMsg22s * sizeof(Msg8a), "Msg6bMsg8as" );
		//try { m_siteRecs = new SiteRec[m_numMsg22s]; }
		//catch ( ... ) {
		//	log("admin: Msg6b could not malloc enough memory.");
		//	return true;
		//}
		//mnew( m_siteRecs, m_numMsg22s * sizeof(SiteRec), "Msg6bSiteRecs" );
		try { m_tagRecs = new TagRec[m_numMsg22s]; }
		catch ( ... ) {
			log("admin: Msg6b could not malloc enough memory.");
			return true;
		}
		mnew( m_tagRecs, m_numMsg22s * sizeof(TagRec), "Msg6bTagRecs" );

	}
	else {
		m_msg8as = NULL;
		//m_siteRecs = NULL;
		m_tagRecs = NULL;
	}

	for(int32_t i = 0; i < m_numMsg22s; i++) {
		m_msg22s[i].m_slot = i;
		m_msg22s[i].m_parent = this;
		if(m_getTagRecs ) {//SiteRecs) {
			m_msg8as[i].m_slotNum = i;
			m_msg8as[i].m_parent = this;
		}
	}



	m_numGotten = 0;
	m_lastSlotUsed = 0;

	bool noBlock = true;
	for(int32_t i = 0; i < m_numToKeep; i++) {
		noBlock &= getMsg22s(i);
		m_lastSlotUsed++;
	}
	return noBlock;
}


bool Msg6b::getMsg22s(int32_t sampleNum) {
	//just send them over the network now, matt seems to think
	//that it will not slow things down much
	int64_t goodDocId = -1;
	if(m_docIdPtr < m_lastDocIdPtr && 
	   m_numOutstanding < m_numToKeep &&
	   m_numGotten < m_numToGet ) {
		m_numOutstanding++;
		goodDocId = *m_docIdPtr++;
	} else {
		return true;
	}
	
	if ( ! m_msg22s[sampleNum].getTitleRec ( NULL ,
						 goodDocId ,
						 true ,
						 m_coll ,
						 m_collLen ,
						 NULL ,
						 false ,
						 false ,
						 false ,
						 &m_msg22s[sampleNum] ,
						 gotMsg22Wrapper,
						 m_niceness ,//niceness
						 false ,
						 false ,
						 60*60*24  , //maxcacheage
						 60 )) {
		return false;
	}
	return gotMsg22s(sampleNum);
}


void gotMsg22Wrapper(void *state) {
	Msg22* m22 = (Msg22*)state;
	Msg6b* THIS = (Msg6b*)m22->m_parent;
	if(THIS->gotMsg22s(m22->m_slot)) {
		THIS->callbackIfDone();
	}
}

bool Msg6b::gotMsg22s(int32_t sampleNum) {
	if(!m_getTagRecs) return gotMsg8as(sampleNum);

	TitleRec* tr = m_msg22s[sampleNum].getTitleRec();
	if(tr->isEmpty()) return getMsg22s(sampleNum);
	if(m_msg22s[sampleNum].m_errno != 0) return getMsg22s(sampleNum);

	
	if ( ! m_msg8as[sampleNum].getTagRec ( tr->getUrl(), 
					       m_coll , 
					       m_collLen ,
					       true    , // useCanonicalName?
					       m_niceness       , // niceness
					       &m_msg8as[sampleNum] ,
					       gotMsg8aWrapper ,
					       &m_tagRecs[sampleNum] ))
		return false;
	return gotMsg8as(sampleNum);
}


void gotMsg8aWrapper(void *state){
	Msg8a* m8a = (Msg8a*)state;
	Msg6b* THIS = (Msg6b*)m8a->m_parent;
	if(THIS->gotMsg8as(m8a->m_slotNum)) {
		THIS->callbackIfDone();
	}
}

//note: we also come here if we were not getting siterecs
bool Msg6b::gotMsg8as(int32_t sampleNum) {
	m_numOutstanding--;
	if(m_msg22s[sampleNum].m_errno != 0) return getMsg22s(sampleNum);

	TitleRec* tr = m_msg22s[sampleNum].getTitleRec();
	if(tr->isEmpty()) return getMsg22s(sampleNum);

	m_numGotten++;

	//we call their callback, if they return true we keep this slot
	//for them, i.e. we can't relaunch a request reusing the titlerec.
	bool keepSlot = false;
	if(m_recordCallback) {
		//SiteRec* sr = NULL;
		TagRec *tagRec = NULL;
		if(m_getTagRecs) tagRec =&m_tagRecs[sampleNum];
		keepSlot = m_recordCallback(m_callbackState, tr, tagRec);
	}


	if(m_numGotten < m_numToGet) {
		if(keepSlot) {
			m_lastSlotUsed++;
			if(m_lastSlotUsed < m_numToKeep)
				return getMsg22s(m_lastSlotUsed);
		}
		return getMsg22s(sampleNum);
	}

	return true;
}


bool Msg6b::callbackIfDone() {
	if(m_numOutstanding != 0) return false;
	m_endCallback(m_callbackState);
	return true;
}
