#include "gb-include.h"
#include "PostQueryRerank.h"
#include "Msg40.h"
#include "LanguageIdentifier.h"
#include "sort.h"
//#include "Thesaurus.h"
//#include "AppendingWordsWindow.h"
//#include "Places.h"
#include "Profiler.h"
#include "CountryCode.h"
#include "Phrases.h"
#include "Linkdb.h"

#define TOTAL_RERANKING_TIME_STR  "PostQueryRerank Total Reranking Time" 

//#define DEBUGGING_LANGUAGE

// Type for post query reranking weighted sort list
struct M20List {
	Msg20 *m_m20;
	//int32_t m_score;
	rscore_t m_score;
	//int m_tier;
	int64_t m_docId;
	char m_clusterLevel;
	//int32_t m_bitScore;
	int32_t m_numCommonInlinks;
	uint32_t m_host;
};

static int32_t s_firstSortFunction( const M20List * a, const M20List * b );
static int32_t s_reSortFunction   ( const M20List * a, const M20List * b );
#ifdef DEBUGGING_LANGUAGE
static void DoDump(char *loc, Msg20 **m20, int32_t num, 
		   score_t *scores, char *tiers);
#endif

bool PostQueryRerank::init ( ) {
	return true;
}

PostQueryRerank::PostQueryRerank ( ) { 
	//log( LOG_DEBUG, "query:in PQR::PQR() AWL" );
	m_enabled            = false;
	m_maxResultsToRerank = 0;

	m_numToSort    = 0;
	m_m20List      = NULL;
	m_positionList = NULL;

	m_msg40 = NULL;

	//m_querysLoc = 0;

	m_maxUrlLen = 0;
	m_pageUrl   = NULL;

	m_now = time(NULL);
}

PostQueryRerank::~PostQueryRerank ( ) {
	//log( LOG_DEBUG, "query:in PQR::~PQR() AWL" );
	if ( m_m20List ) {
		mfree( m_m20List, sizeof(M20List) * m_maxResultsToRerank, 
		       "PostQueryRerank" );
		m_m20List = NULL;
	}
	if ( m_positionList ) {
		mfree( m_positionList, sizeof(int32_t) * m_maxResultsToRerank,
		       "PQRPosList" );
		m_positionList = NULL;
	}

	if ( m_cvtUrl ) mfree( m_cvtUrl, m_maxUrlLen, "pqrcvtUrl") ;
	if ( m_pageUrl ) mfree( m_pageUrl, sizeof(Url)*m_maxResultsToRerank,
				"pqrpageUrls" );
}

// returns false on error 
bool PostQueryRerank::set1 ( Msg40 *msg40, SearchInput *si ) {
	//log(LOG_DEBUG, "query:in PQR::set1(%p) AWL", msg40);

	m_msg40 = msg40;
	m_si    = si;

	if ( ! m_msg40 ) return false;
	if ( ! m_si ) return false;
	if ( ! m_si->m_cr ) return false;

	m_enabled = (m_si->m_docsToScanForReranking > 1);
	//log( LOG_DEBUG, "query:  m_isEnabled:%"INT32"; "
	//     "P_docsToScanForReranking:%"INT32" P_pqr_docsToSan:%"INT32"; AWL",
	//     (int32_t)m_enabled, 
	//     m_si->m_docsToScanForReranking,
	//     m_si->m_cr->m_pqr_docsToScan );

	return m_enabled;
}

// must be called sometime after we know numDocIds and before preRerank
// returns false if we shouldn't rerank
bool PostQueryRerank::set2 ( int32_t resultsWanted ) {
	//log(LOG_DEBUG, "query:in PQR::set2() AWL");

	//log( LOG_DEBUG, "query: firstResultNum:%"INT32"; numResults:%"INT32"; "
	//     "wanted:%"INT32" numMsg20s:%"INT32" AWL", 
	//     m_msg40->getFirstResultNum(), m_msg40->getNumResults(),
	//     resultsWanted, m_msg40->m_numMsg20s );

	// we only want to check the lessor of docsToScan and numDocIds
	m_maxResultsToRerank = m_si->m_docsToScanForReranking;
	if ( m_maxResultsToRerank > m_msg40->getNumDocIds() ) {
		m_maxResultsToRerank = m_msg40->getNumDocIds();
		log( LOG_DEBUG, "pqr: request to rerank more results "
		     "than the number of docids, capping number to rerank "
		     "at %"INT32"", m_maxResultsToRerank );
	}

	// If we don't have less results from clustering / deduping or
	// we have less results in docids then ...
	if ( m_msg40->getNumResults() < m_msg40->getNumDocIds() &&
	     m_msg40->getNumResults() < resultsWanted ) 
		return false;

	// are we passed pqr's range?
	if ( m_msg40->getFirstResultNum() > m_maxResultsToRerank ) 
		return false;

	// Safety check, make sure there are less results to rerank
	// than the number of Msg20s
	if ( m_msg40->m_numMsg20s < m_maxResultsToRerank )
		m_maxResultsToRerank = m_msg40->m_numMsg20s;

	//log( LOG_DEBUG, "query: m_maxResultsToRerank:%"INT32" AWL", 
	//     m_maxResultsToRerank );

	if ( m_maxResultsToRerank < 2 ) {
		//log( LOG_INFO, "pqr: too few results to rerank" );
		return false;
	}

	if ( m_maxResultsToRerank > 250 ) {
		log( LOG_INFO, "pqr: too many results to rerank, "
		     "capping at 250" );
		m_maxResultsToRerank = 250;
	}

	// see if we are done
	if ( m_msg40->getFirstResultNum() >= m_maxResultsToRerank ) {
		log( LOG_INFO, "pqr: first result is higher than max "
		     "results to rerank" );
		return false;
	}
	
	// get space for host count table
	m_hostCntTable.set( m_maxResultsToRerank );
	
	// get some space for dmoz table
	m_dmozTable.set( m_maxResultsToRerank << 1 );

	// alloc urls for pqrqttiu, pqrfsh and clustering
	m_pageUrl = (Url *)mcalloc( sizeof(Url)*m_maxResultsToRerank,
				    "pqrpageUrls" );

	if ( ! m_pageUrl ) {
		log("pqr: had out of memory error");
		return false;
	}

	return true;
}

#include "Sanity.h"

// sets up PostQueryRerank for each page in m_maxResultsToRerank
// returns false on error
bool PostQueryRerank::preRerank ( ) {
  //if ( g_conf.m_profilingEnabled ) {
  //	g_profiler
  //		.startTimer((int32_t)(this->*(&PostQueryRerank::rerank)), 
  //			    TOTAL_RERANKING_TIME_STR );
  //}
	//log( LOG_DEBUG, "query:in PQR::preRerank() AWL" );

#ifdef DEBUGGING_LANGUAGE
	DoDump( "Presort", m_msg40->m_msg20, m_maxResultsToRerank, 
		m_msg40->m_msg3a.m_scores, NULL);//m_msg40->m_msg3a.m_tiers );
#endif

	if( m_si->m_enableLanguageSorting 
	    && !m_si->m_langHint ) 
		log( LOG_INFO, "pqr: no language set for sort. "
		"language will not be reranked" );

	GBASSERT( ! m_m20List );
	m_m20List = (M20List*)mcalloc( sizeof(M20List) * m_maxResultsToRerank,
				       "PostQueryRerank" );
	if( ! m_m20List ) {
		log( LOG_INFO, "pqr: Could not allocate PostQueryRerank "
		     "sort memory.\n" );
		g_errno = ENOMEM;
		return(false);
	}
	GBASSERT( ! m_positionList );
	m_positionList = (int32_t *)mcalloc( sizeof(int32_t) * m_maxResultsToRerank,
					  "PQRPosList" );
	if( ! m_positionList ) {
		log( LOG_INFO, "pqr: Could not allocate PostQueryRerank "
		     "postion list memory.\n" );
		g_errno = ENOMEM;
		return(false);
	}

	//log(LOG_DEBUG, "pqr: the query is '%s' AWL", m_si->m_q->m_orig);

	// setup for rerankNonLocationSpecificQueries if enabled
	//if ( ! preRerankNonLocationSpecificQueries() )
	//	return false;

	// . make a temp hash table for iptop
	// . each slot is a int32_t key and a int32_t value
	HashTable ipTable;
	// how many slots
	int32_t numSlots = 5000 / ((4+4)*4);
	char tmp[5000];
	// this should NEVER need to allocate, UNLESS for some reason we got
	// a ton of inlinking ips
	if ( ! ipTable.set ( numSlots , tmp , 5000 ) ) return false;
	// this table maps a docid to the number of search results it links to
	HashTableT <int64_t, int32_t> inlinkTable;
	char tmp2[5000];
	int32_t numSlots2 = 5000 / ((8+4)*4);
	if ( ! inlinkTable.set ( numSlots2 , tmp2 , 5000 ) ) return false;

	// Fill sort array
	int32_t y = 0;
	for( int32_t x = 0; 
	     x < m_msg40->m_numMsg20s && y < m_maxResultsToRerank;
	     x++ ) {
		// skip clustered out results
		char clusterLevel = m_msg40->getClusterLevel( x );
		if ( clusterLevel != CR_OK ) {
			//log( LOG_DEBUG, "pqr: skipping result "
			//     "%"INT32" since cluster level(%"INT32") != "
			//     "CR_OK(%"INT32") AWL",
			//     x, (int32_t)clusterLevel, (int32_t)CR_OK );
			continue;
		}
		// skip results that don't match all query terms
		//int32_t bitScore = m_msg40->getBitScore( x );
		//if ( bitScore == 0x00 ) continue;

		// . save postion of this result so we can fill it in later
		//   with (possibly) a higher ranking result
		m_positionList[y] = x;

		M20List *sortArrItem = &m_m20List [ y ];
		sortArrItem->m_clusterLevel = clusterLevel                  ;
		sortArrItem->m_m20          = m_msg40->m_msg20         [ x ];
		sortArrItem->m_score        = (rscore_t)m_msg40->getScore(x);
		//sortArrItem->m_tier         = m_msg40->getTier         ( x );
		sortArrItem->m_docId        = m_msg40->getDocId        ( x );
		//sortArrItem->m_bitScore     = bitScore                      ;
		sortArrItem->m_host         = 0; // to be filled in later

		Msg20 *msg20 = sortArrItem->m_m20;
		GBASSERT( msg20 && ! msg20->m_errno );

		Msg20Reply *mr = msg20->m_r;

		// set the urls for each page
		// used by pqrqttiu, pqrfsh and clustering
		m_pageUrl[y].set( mr->ptr_ubuf , false );
		// now fill in host without the 'www.' if present
		char *host    = m_pageUrl[y].getHost();
		int32_t  hostLen = m_pageUrl[y].getHostLen();
		if (hostLen > 4 &&
		    host[3] == '.' &&
		    host[0] == 'w' && host[1] == 'w' && host[2] == 'w')
			sortArrItem->m_host = hash32(host+4, hostLen-4);
		else
			sortArrItem->m_host = hash32(host, hostLen);

		// add its inlinking docids into the hash table, inlinkTable
		LinkInfo *info = (LinkInfo *)mr->ptr_linkInfo;//inlinks;
		//int32_t       n         = msg20->getNumInlinks      ();
		//int64_t *docIds    = msg20->getInlinkDocIds    ();
		//char      *flags     = msg20->getInlinkFlags     ();
		//int32_t      *ips       = msg20->getInlinkIps       ();
		//char      *qualities = msg20->getInlinkQualities ();
		// skip adding the inlinking docids if search result has bad ip
		int32_t ip = mr->m_ip;//msg20->getIp();
		bool good = true;
		if ( ip ==  0 ) good = false;
		if ( ip == -1 ) good = false;
		// . skip inlinker add already did this "ip top"
		// . "ip top" is the most significant 3 bytes of the ip
		// . get the ip of the docid:
		int32_t top = iptop ( ip );
		// if we already encountered a higher-scoring search result
		// with the same iptop, do not count its inlinkers!
		// so that if an inlinker links to two docids in the search 
		// results, where those two docids are from the same 
		// "ip top" then the docid is only "counted" once here.
		if ( ipTable.getSlot ( top ) >= 0 ) good = false;
		// not allowed to be 0
		if ( top == 0 ) top = 1;
		// now add to table so no we do not add the inlinkers from
		// any other search results from the same "ip top"
		if ( ! ipTable.addKey ( top , 1 ) ) return false;
		// now hash all the inlinking docids into inlinkTable
		for ( Inlink *k=NULL; good && (k=info->getNextInlink(k) ) ; ) {
			// lower score if it is link spam though
			if ( k->m_isLinkSpam ) continue;
			// must be quality of 35 or higher to "vote"
			//if ( k->m_docQuality < 35 ) continue;
			if ( k->m_siteNumInlinks < 20 ) continue;
			// skip if bad ip for inlinker
			if ( k->m_ip == 0 || k->m_ip == -1 ) continue;
			// skip if inlinker has same top ip as search result
			if ( iptop(k->m_ip) == top ) continue;
			// get the current slot in table from docid of inlinker
			int32_t slot = inlinkTable.getSlot ( k->m_docId );
			// get the score
			if ( slot >= 0 ) {
				int32_t count=inlinkTable.getValueFromSlot(slot);
				inlinkTable.setValue ( slot , count + 1 );
				continue;
			}
			// add it fresh if not already in there
			if (!inlinkTable.addKey(k->m_docId,1)) return false;
		}

		//log( LOG_DEBUG, "pqr: pre: setting up sort array - "
		//     "mapping x:%"INT32" to y:%"INT32"; "
		//     "url:'%s' (%"INT32"); tier:%d; score:%"INT32"; "
		//     "docId:%lld; clusterLevel:%d; AWL",
		//     x, y, 
		//     msg20->getUrl(), msg20->getUrlLen(), 
		//     sortArrItem->tier, sortArrItem->score,
		//     sortArrItem->docId, sortArrItem->clusterLevel );

		// setup reranking for pages from the same host (pqrfsd)
		if ( ! preRerankOtherPagesFromSameHost( &m_pageUrl[y] ))
			return false;
		
		// setup reranking for pages with common topics in dmoz (pqrctid)
		if ( ! preRerankCommonTopicsInDmoz( mr ) )
			return false;

		// . calculate maximum url length in pages for reranking 
		//   by query terms or topics in a url
		int32_t urlLen = mr->size_ubuf - 1;//msg20->getUrlLen();
		if ( urlLen > m_maxUrlLen )
			m_maxUrlLen = urlLen;

		// update num to rerank and sort
		m_numToSort++;
		y++;
	}

	// get the max
	m_maxCommonInlinks = 0;
	// how many of OUR inlinkers are shared by other results?
	for ( int32_t i = 0; i < m_numToSort; i++ ) {
		// get the item
		M20List *sortArrItem = &m_m20List [ i ];
		Msg20 *msg20 = sortArrItem->m_m20;
		// reset
		sortArrItem->m_numCommonInlinks = 0;
		// lookup its inlinking docids in the hash table
		//int32_t       n      = msg20->getNumInlinks   ();
		//int64_t *docIds = msg20->getInlinkDocIds ();
		LinkInfo *info = (LinkInfo *)msg20->m_r->ptr_linkInfo;
		for ( Inlink *k=NULL;info&&(k=info->getNextInlink(k)) ; ) {
			// how many search results does this inlinker link to?
			int32_t*v=(int32_t *)inlinkTable.getValuePointer(k->m_docId);
			if ( ! v ) continue;
			// if only 1 result had this as an inlinker, skip it
			if ( *v <= 1 ) continue;
			// ok, give us a point
			sortArrItem->m_numCommonInlinks++;
		}
		// get the max
		if ( sortArrItem->m_numCommonInlinks > m_maxCommonInlinks )
			m_maxCommonInlinks = sortArrItem->m_numCommonInlinks;
	}


	// . setup reranking for query terms or topics in url (pqrqttiu)
	// . add space to max url length for terminating NULL and allocate
	//   room for max length
	m_maxUrlLen++;
	m_cvtUrl = (char *)mmalloc( m_maxUrlLen, "pqrcvtUrl" );
	if ( ! m_cvtUrl ) {
		log( LOG_INFO, "pqr: Could not allocate %"INT32" bytes "
		     "for m_cvtUrl.",
		     m_maxUrlLen );
		g_errno = ENOMEM;
		return false;
	}

	// Safety valve, trim sort results
	if ( m_numToSort > m_maxResultsToRerank )
		m_numToSort = m_maxResultsToRerank;

	//log( LOG_DEBUG, "pqr::m_numToSort:%"INT32" AWL", m_numToSort );

	return true;
}

// perform actual reranking of m_numToSort pages
// returns false on error
bool PostQueryRerank::rerank ( ) {
	//log(LOG_DEBUG,"query:in PQR::rerank() AWL");
	if(m_si->m_debug||g_conf.m_logDebugPQR )
		logf( LOG_DEBUG, "pqr: reranking %"INT32" results", 
		     m_numToSort );

	/*
	float maxDiversity = 0;
	if(m_si->m_pqr_demFactSubPhrase > 0) {
		for ( int32_t x = 0; x < m_numToSort; x++ ) {
			M20List *sortArrItem = &m_m20List [ x ];
			Msg20 *msg20 = sortArrItem->m_m20;
			if ( ! msg20 || msg20->m_errno ) continue;
			float d = msg20->m_r->m_diversity;
			if(d > maxDiversity) maxDiversity = d;
		}
	}

	float maxProximityScore = 0;
	float minProximityScore = -1.0;
	//float maxInSectionScore = 0;
	if(m_si->m_pqr_demFactProximity > 0 ||
	   m_si->m_pqr_demFactInSection > 0) {
		//grab the max score so that we know what the max to 
		//demote is.
		for ( int32_t x = 0; x < m_numToSort; x++ ) {
			M20List *sortArrItem = &m_m20List [ x ];
			Msg20 *msg20 = sortArrItem->m_m20;
			if ( ! msg20 || msg20->m_errno ) continue;
			//float d = msg20->m_r->m_inSectionScore;
			//if(d > maxInSectionScore) 
			//	maxInSectionScore = d;
			// handle proximity
			float d = msg20->m_r->m_proximityScore;
			// i think this means it does not have all the query 
			// terms! for 'sylvain segal' we got 
			// www.regalosdirectos.tv/asp2/comparar.asp?cat=36 
			// in results
			if ( d == 0.0 ) continue;
			// . -1 is a bogus proximity
			// . it means we were not able to find all the terms
			//   because they were in anomalous link text or
			//   meta tags or select tags or whatever... so for
			//   now such results will not be demoted to be on the
			//   safe side
			if ( d == -1.0 ) continue;
			if ( d > maxProximityScore ) 
				maxProximityScore = d;
			if ( d < minProximityScore || minProximityScore==-1.0 )
				minProximityScore = d;
		}
	}
	*/


	// rerank weighted sort list
	for ( register int32_t x = 0; x < m_numToSort; x++ ) {
		M20List *sortArrItem = &m_m20List [ x ];
		Msg20 *msg20 = sortArrItem->m_m20;
		char *url = NULL;
		rscore_t score = sortArrItem->m_score;
		rscore_t startScore = score;

		// mwells: what is this?
 		if(m_si->m_pqr_demFactOrigScore < 1) {
 		//turn off the indexed score and just use a uniform start score
 		//because I can't get the proximity pqr to overwhelm the 
 		//preexisting score.
 			score = 1000000 + (m_numToSort - x) + 
				(int32_t)(score * m_si->m_pqr_demFactOrigScore);
			startScore = score;
 		}

		// if don't have a good msg20, skip reranking for this result
		if ( ! msg20 || msg20->m_errno )
			continue;

		url = msg20->m_r->ptr_ubuf;//getUrl();
		if ( ! url ) url = "(none)";
		if(m_si->m_debug||g_conf.m_logDebugPQR )
			logf(LOG_DEBUG, "pqr: result #%"INT32":'%s' has initial "
			     "score of %.02f", 
			     x, url, (float)startScore );

		// resets
		msg20->m_pqr_old_score        = score;
		msg20->m_pqr_factor_quality   = 1.0;
		msg20->m_pqr_factor_diversity = 1.0;
		msg20->m_pqr_factor_inlinkers = 1.0;
		msg20->m_pqr_factor_proximity = 1.0;
		msg20->m_pqr_factor_ctype     = 1.0;
		msg20->m_pqr_factor_lang      = 1.0; // includes country

		Msg20Reply *mr = msg20->m_r;

		// demote for language and country
		score =	rerankLanguageAndCountry( score,
						  mr->m_language ,
						  mr->m_summaryLanguage,
						  mr->m_country, // id
						  msg20 );

		// demote for content-type
		float htmlFactor = m_si->m_cr->m_pqr_demFactNonHtml;
		float xmlFactor  = m_si->m_cr->m_pqr_demFactXml;
		int32_t  contentType= mr->m_contentType;
		if ( contentType == CT_XML && xmlFactor > 0 ) {
			score = score * xmlFactor;
			msg20->m_pqr_factor_ctype = xmlFactor;
		}
		else if ( contentType != CT_HTML && htmlFactor > 0 ) {
			score = score * htmlFactor;
			msg20->m_pqr_factor_ctype = htmlFactor;
		}
		//if ( score == 1 ) goto finishloop;

		// demote for fewer query terms or gigabits in url
		//score =	rerankQueryTermsOrGigabitsInUrl( score,
		//					 &m_pageUrl[x] );
		

		// . demote for not high quality
		// . multiply by "qf" for every quality point below 100
		// . now we basically do this if we have a wiki title
		// . float qf = m_si->m_cr->m_pqr_demFactQual;
		/*
		if ( m_msg40->m_msg3a.m_oneTitle ) {
			//int32_t q = msg20->getQuality();
			int32_t sni = mr->m_siteNumInlinks;
			if ( sni <= 0 ) sni = 1;
			float weight = 1.0;
			for ( ; sni < 100000 ; sni *= 2 ) 
				weight = weight * 0.95;
			// apply the weight to the score
			score = score * weight;
			// store that for print in PageResults.cpp
			msg20->m_pqr_factor_quality = weight;
		}
		*/

		// demote for more paths in url
		score = rerankPathsInUrl( score,
					  msg20->m_r->ptr_ubuf,//getUrl(),
					  msg20->m_r->size_ubuf-1 );

		// demote for smallest cat id has a lot of super topics
		score = rerankSmallestCatIdHasSuperTopics( score,
							   msg20 );

		// demote for larger page sizes
		score = rerankPageSize( score,
					msg20->m_r->m_contentLen );

		// . demote for non location specific queries that have an
		//   an obvious location in gigabits or url
		//score = rerankNonLocationSpecificQueries( score,
		//					  msg20 );
		//if ( score == 1 ) goto finishloop;

		// demote for no cat id
		score = rerankNoCatId( score,
				       msg20->m_r->size_catIds/4,
				       msg20->m_r->size_indCatIds/4);

		// demote for no other pages from same host
		score = rerankOtherPagesFromSameHost( score,
						      &m_pageUrl[x] );

		// demote for fewer common topics in dmoz
		score = rerankCommonTopicsInDmoz( score,
						  msg20 );

		// . demote for pages with dmoz category names do not
		//   contain a query term
		//score = rerankDmozCategoryNamesDontHaveQT( score,
		//					   msg20 );

		// . demote for pages with dmoz category names do not
		//   contain a query term
		//score = rerankDmozCategoryNamesDontHaveGigabits( score,
		//						 msg20 );

		// . demote pages for older datedb dates
		score = rerankDatedbDate( score,
					  msg20->m_r->m_datedbDate );

		/*
		// . demote pages by proximity
		// . a -1 prox implies did not have any query terms
		// . see Summary.cpp proximity algo
		float ps = msg20->m_r->m_proximityScore;//getProximityScore();
		if ( ps > 0.0 && 
		     m_si->m_pqr_demFactProximity > 0 &&
		     minProximityScore != -1.0 ) {
			// what percent were we of the max?
			float factor = minProximityScore / ps ;
			// this can be weighted
			//factor *= m_si->m_pqr_demFactProximity;
			// apply the factor to the score
			score *= factor;
			// this is the factor
			msg20->m_pqr_factor_proximity = factor;
		}

		// . demote pages by the average of the scores of the
		// . terms based upon what section of the doc they are in
		// . mdw: proximity algo should obsolete this
		//if(maxInSectionScore > 0)
		//	score = rerankInSection( score,
		//				 msg20->getInSectionScore(),
		//				 maxInSectionScore);


		// . demote pages which only have the query as a part of a
		// . larger phrase
		if ( maxDiversity != 0 ) {
			float diversity = msg20->m_r->m_diversity;
			float df = (1 - (diversity/maxDiversity)) *
				m_si->m_pqr_demFactSubPhrase;
			score = (rscore_t)(score * (1.0 - df));
			if ( score <= 0.0 ) score = 0.001;
			msg20->m_pqr_factor_diversity = 1.0 - df;
		}
		*/

		// . COMMON INLINKER RERANK
		// . no need to create a superfluous function call here
		// . demote pages that do not share many inlinking docids
		//   with other pages in the search results
		if ( m_maxCommonInlinks>0 && m_si->m_pqr_demFactCommonInlinks){
			int32_t nc = sortArrItem->m_numCommonInlinks ;
			float penalty;
			// the more inlinkers, the less the penalty
			penalty = 1.0 -(((float)nc)/(float)m_maxCommonInlinks);
			// . reduce the penalty for higher quality pages
			// . they are the most likely to have their inlinkers 
			//   truncated
			//char quality = msg20->getQuality();
			float sni = (float)msg20->m_r->m_siteNumInlinks;
			// decrease penalty for really high quality docs
			//while ( quality-- > 60 ) penalty *= .95;
			for ( ; sni > 1000 ; sni *= .80 ) penalty *= .95;
			// if this parm is 0, penalty will become 0
			penalty *= m_si->m_pqr_demFactCommonInlinks;
			// save old score
			score = score * (1.0 - penalty);
			// do not decrease all the way to 0!
			if ( score <= 0.0 ) score = 0.001;
			// store it!
			msg20->m_pqr_factor_inlinkers = 1.0 - penalty;
		}

		//	finishloop:
		if(m_si->m_debug || g_conf.m_logDebugPQR )
			logf( LOG_DEBUG, "pqr: result #%"INT32"'s final "
			     "score is %.02f (-%3.3f%%) ",
			     x, (float)score,100-100*(float)score/startScore );
		sortArrItem->m_score = score;
	}

	return(true);
}

// perform post reranking tasks 
// returns false on error 
bool PostQueryRerank::postRerank ( ) {
	//log( LOG_DEBUG, "query:in PQR::postRerank() AWL" );

	// Hopefully never happen...
	//log( LOG_DEBUG, "query: just before sort: "
	//     "m_maxResultsToRerank:%"INT32" m_numToSort:%"INT32" AWL", 
	//     m_maxResultsToRerank, m_numToSort);
	if ( m_numToSort < 0 ) return false;

	// Sort the array
	gbmergesort( (void *) m_m20List, (size_t) m_numToSort, 
		     (size_t) sizeof(M20List),
		     (int (*)(const void *, const void *))s_firstSortFunction);

	// move 2nd result from a particular domain to just below the first
	// result from that domain if it is within 10 results of the first
	//XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX put this back in after debugging summary rerank!
	//XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
	//if (!attemptToCluster()) return false;

	// Fill result arrays with our reranked results
	for( int32_t y = 0; y < m_numToSort; y++ ) {
		M20List *a = &m_m20List     [ y ];
		int32_t     x = m_positionList [ y ];
		m_msg40->m_msg20                 [ x ] = a->m_m20;
		//m_msg40->m_msg3a.m_tiers         [ x ] = a->m_tier;
		m_msg40->m_msg3a.m_scores        [ x ] = a->m_score;
		m_msg40->m_msg3a.m_docIds        [ x ] = a->m_docId;
		m_msg40->m_msg3a.m_clusterLevels [ x ] = a->m_clusterLevel;
		//log( LOG_DEBUG, "pqr: post: mapped y:%"INT32" "
		//     "to x:%"INT32" AWL",
		//     y, x );
	}

#ifdef DEBUGGING_LANGUAGE
	DoDump( "Postsort", m_msg40->m_msg20, m_numToSort, 
	        m_msg40->m_msg3a.m_scores, NULL );//m_msg40->m_msg3a.m_tiers );
#endif

	//if ( ! g_conf.m_profilingEnabled ) return true;
	//if ( ! g_profiler.endTimer( (int32_t)(this->*(&PostQueryRerank::rerank)), 
	//			    TOTAL_RERANKING_TIME_STR) )
	//	log( LOG_WARN,"admin: Couldn't add the fn %"INT32"",
	//	     (int32_t)(this->*(&PostQueryRerank::rerank)) );
	return true;
}

// called if we weren't able to rerank for some reason
void PostQueryRerank::rerankFailed ( ) {
  //if ( g_conf.m_profilingEnabled ) {
  //	if( ! g_profiler
  //	    .endTimer( (int32_t)(this->*(&PostQueryRerank::rerank)), 
  //		       TOTAL_RERANKING_TIME_STR) )
  //		log(LOG_WARN,"admin: Couldn't add the fn %"INT32"",
  //		    (int32_t)(this->*(&PostQueryRerank::rerank)));
  //}
}

// lsort (pqrlang, pqrlangunk, pqrcntry)
// rerank for language, then country
rscore_t PostQueryRerank::rerankLanguageAndCountry ( rscore_t score, 
						 uint8_t lang,
						 uint8_t summaryLang,
						 uint16_t country ,
						     Msg20 *msg20 ) { 
	//log( LOG_DEBUG, "query:in PQR::rerankLanguageAndCountry("
	//     "score:%"INT32", lang:%"INT32", summLang:%"INT32", country:%"INT32")"
	//     "[langSortingIsOn:%"INT32"; langUnkWeight:%3.3f; langWeight:%3.3f; "
	//     "&qlang=%"INT32"; &lang=%"INT32"; "
	//     "&qcountry=%"INT32"; &gbcountry=%"INT32"; "
	//     "queryLangs:%lld; pageLangs:%lld] AWL",
	//     score, (int32_t)lang, (int32_t)summaryLang, (int32_t)country,
	//     (int32_t)m_si->m_enableLanguageSorting,
	//     m_si->m_languageUnknownWeight,
	//     m_si->m_languageWeightFactor,
	//     (int32_t)m_si->m_langHint,
	//     (int32_t)m_si->m_language,
	//     (int32_t)m_si->m_countryHint,
	//     (int32_t)m_si->m_country,
	//     g_countryCode.getLanguagesWritten( m_si->m_countryHint ),
	//     g_countryCode.getLanguagesWritten( country ) );
	
	// if lsort is off, skip
	if ( ! m_si->m_enableLanguageSorting ) return score;

	// . use query lanaguage (si->m_langHint) or restricted search 
	//   language (si->m_language)
	// . if both are 0, don't rerank by language
	uint8_t langWanted = m_si->m_langHint;
	if ( langWanted == langUnknown ) langWanted = m_si->m_queryLangId;
	if ( langWanted == langUnknown ) return score;

	// . apply score factors for unknown languages, iff reranking unknown
	//   languages
	if ( lang == langUnknown &&
	     m_si->m_languageUnknownWeight > 0 ) {
		msg20->m_pqr_factor_lang =m_si->m_languageUnknownWeight;
		return rerankAssignPenalty(score, 
					   m_si->m_languageUnknownWeight,
					   "pqrlangunk", 
					   "it's language is unknown" );
	}

	// . if computed lanaguage is unknown, don't penalize
	// . no, what if from a different country?
	if ( summaryLang == langUnknown ) return score;
		
	// . first, apply score factors for non-preferred summary languages 
	//   that don't match the page language
	if ( summaryLang != langUnknown && summaryLang != langWanted ) {
		msg20->m_pqr_factor_lang = m_si->m_languageWeightFactor;
		return rerankAssignPenalty( score, 
					    m_si->m_languageWeightFactor,
					    "pqrlang", 
					    "it's summary/title "
					    "language is foreign" );
	}
	
	// second, apply score factors for non-preferred page languages
	//if ( lang != langWanted )
	//	return rerankAssignPenalty( score, 
	//				    m_si->m_languageWeightFactor,
	//				    "pqrlang", 
	//				    "it's page language is foreign" );
	
	// . if we got here languages of query and page match and are not
	//   unknown, so rerank based on country
	// . don't demote if countries match or either the search country
	//   or page country is unknown (0)
	// . default country wanted to gbcountry parm if not specified
	uint8_t countryWanted = m_si->m_countryHint;
	// SearchInput sets m_country based on the IP address of the incoming
	// query, which is often wrong, especially for internal 10.x.y.z ips.
	// so just fallback to countryHint for now bcause that uses teh default
	// country... right now set to "us" in search controls page.
	if ( countryWanted == 0 ) countryWanted = m_si->m_country;
	if ( country == 0 || countryWanted == 0 ||
	     country == countryWanted )
		return score;

	// . now, languages match and are not unknown and countries don't 
	//   match and neither is unknown
	// . so, demote if country of query speaks the same language as 
	//   country of page, ie US query and UK or AUS page (since all 3
	//   places speak english), but not US query and IT page
	uint64_t qLangs = g_countryCode.getLanguagesWritten( countryWanted );
	uint64_t pLangs = g_countryCode.getLanguagesWritten( country );
	// . if no language written by query country is written by page 
	//   country, don't penalize
	if ( (uint64_t)(qLangs & pLangs) == (uint64_t)0LL ) return score;

	msg20->m_pqr_factor_lang = m_si->m_cr->m_pqr_demFactCountry;

	// countries do share at least one language - demote!
	return rerankAssignPenalty( score,
				    m_si->m_cr->m_pqr_demFactCountry,
				    "pqrcntry",
				    "it's language is the same as that of "
				    "of the query, but it is from a country "
				    "foreign to that of the query which "
				    "writes in at least one of the same "
				    "languages" );
}
 
// pqrqttiu
// . look for query terms and gigabits in the url, demote more the fewer 
//   are matched.
/*
rscore_t PostQueryRerank::rerankQueryTermsOrGigabitsInUrl( rscore_t score,
							    Url *pageUrl ) {
	//log( LOG_DEBUG, "query:in PQR::rerankQueryTermsOrGigabitsInUrl("
	//     "score:%"INT32", url:'%s', urlLen:%"INT32")"
	//     "[factor:%3.3f; max:%"INT32"] AWL", 
	//     score, pageUrl->getUrl(), pageUrl->getUrlLen(),
	//     m_si->m_cr->m_pqr_demFactQTTopicsInUrl,
	//     m_si->m_cr->m_pqr_maxValQTTopicsInUrl );

	if ( pageUrl->getUrlLen() == 0 ) return score;

	float factor = m_si->m_cr->m_pqr_demFactQTTopicsInUrl;
	if ( factor <= 0 ) return score; // disables
	int32_t maxQTInUrl = m_si->m_q->getNumTerms(); 
	int32_t maxGigabitsInUrl = m_msg40->getNumTopics();
	int32_t maxVal = m_si->m_cr->m_pqr_maxValQTTopicsInUrl;
	if ( maxVal < 0 ) maxVal = maxQTInUrl+maxGigabitsInUrl; 

	// from original url:
	// . remove scheme
	// . remove 'www' from host
	// . remove tld
	// . remove ext
	// . convert symbols to spaces
	// . remove extra space
	//log( LOG_DEBUG, "query: origurl:'%s' AWL", pageUrl->getUrl() );
	//log( LOG_DEBUG, "query: url: whole:'%s' host:'%s' (%"INT32"); "
	//     "domain:'%s' (%"INT32"); tld:'%s' (%"INT32"); midDom:'%s' (%"INT32"); "
	//     "path:'%s' (%"INT32"); fn:'%s'; ext:'%s'; query:'%s' (%"INT32"); "
	//     "ipStr:'%s' {%"INT32"}; anch:'%s' (%"INT32") "
	//     "site:'%s' (%"INT32") AWL",
	//     pageUrl->getUrl(),
	//     pageUrl->getHost(), pageUrl->getHostLen(),
	//     pageUrl->getDomain(), pageUrl->getDomainLen(),
	//     pageUrl->getTLD(), pageUrl->getTLDLen(),
	//     pageUrl->getMidDomain(),  pageUrl->getMidDomainLen(),
	//     pageUrl->getPath(), pageUrl->getPathLen(),
	//     pageUrl->getFilename(), pageUrl->getExtension(), 
	//     pageUrl->getQuery(), pageUrl->getQueryLen(),
	//     pageUrl->getIpString(), pageUrl->getIp(),
	//     pageUrl->getAnchor(), pageUrl->getAnchorLen(),
	//     pageUrl->getSite(), pageUrl->getSiteLen() );
	m_cvtUrl[0] = '\0';
	int32_t cvtUrlLen = 0;
	char *host = pageUrl->getHost();
	// first, add hostname - "www." iff it is not an ip addr
	if ( pageUrl->getIp() == 0 ) {
		if ( host[0] == 'w' && host[1] == 'w' && host[2] == 'w' && 
		     host[3] == '.' ) {
			// if starts with 'www.', don't add the 'www.'
			if(pageUrl->getHostLen()-pageUrl->getDomainLen() == 4){
				// add domain - 'www.' - tld 
				strncpy( m_cvtUrl, pageUrl->getDomain(), 
					 pageUrl->getDomainLen() - 
					 pageUrl->getTLDLen() );
				cvtUrlLen += pageUrl->getDomainLen() - 
					pageUrl->getTLDLen();
				m_cvtUrl[cvtUrlLen] = '\0';
			}
			else {
				// add host + domain - 'www.' - tld
				strncpy( m_cvtUrl, pageUrl->getHost()+4,
					 pageUrl->getHostLen() - 
					 pageUrl->getTLDLen() - 4 );
				cvtUrlLen += pageUrl->getHostLen() - 
					pageUrl->getTLDLen() - 4;
				m_cvtUrl[cvtUrlLen] = '\0';
			}
		}
		else {
			// add host + domain - tld
			strncpy( m_cvtUrl, pageUrl->getHost(),
				 pageUrl->getHostLen() - 
				 pageUrl->getTLDLen() - 1 );
			cvtUrlLen += pageUrl->getHostLen() - 
				pageUrl->getTLDLen() - 1;
			m_cvtUrl[cvtUrlLen] = '\0';
		}
		
	}
	// next, add path
	if ( pageUrl->getPathLen() > 0 ) {
		strncat( m_cvtUrl, pageUrl->getPath(), 
			 pageUrl->getPathLen()-pageUrl->getExtensionLen() );
		cvtUrlLen += pageUrl->getPathLen()-pageUrl->getExtensionLen();
		m_cvtUrl[cvtUrlLen] = '\0';
	}
	// next, add query
	if ( pageUrl->getQueryLen() > 0 ) {
		strncat( m_cvtUrl, pageUrl->getQuery(), pageUrl->getQueryLen() );
		cvtUrlLen += pageUrl->getQueryLen();
		m_cvtUrl[cvtUrlLen] = '\0';
	}
	// remove all non-alpha-numeric chars
	char *t = m_cvtUrl;
	for ( char *s = m_cvtUrl; *s; s++ ) {
		if ( is_alnum_a(*s) ) *t++ = *s;
		else if ( t>m_cvtUrl && *(t-1) != ' ' ) *t++ = ' ';
	}
	*t = '\0';
	cvtUrlLen = (t-m_cvtUrl);
	//log( LOG_DEBUG, "query:  m_cvtUrl:'%s' (%"INT32") AWL", 
	//     m_cvtUrl, cvtUrlLen );

	// find number of query terms in url
	int32_t numQTInUrl = 0;
	int32_t numQTs = m_si->m_q->getNumTerms();
	for ( int32_t i = 0; i < numQTs; i++ ) {
		char *qtStr = m_si->m_q->getTerm(i);
		int32_t  qtLen = m_si->m_q->getTermLen(i);
		if ( strncasestr(m_cvtUrl, qtStr, cvtUrlLen, qtLen) != NULL ) {
			numQTInUrl++;
			//log( LOG_DEBUG, "query:  qt is in url AWL");
		}
	}

	// find number of gigabits in url
	int32_t numGigabitsInUrl = 0;
	int32_t numTopics = m_msg40->getNumTopics();
	for ( int32_t i = 0; i < numTopics; i++ ) {
		char *topicStr = m_msg40->getTopicPtr(i);
		int32_t  topicLen = m_msg40->getTopicLen(i);
		if ( strncasestr(m_cvtUrl, topicStr, cvtUrlLen, topicLen) ) {
			numGigabitsInUrl++;
			//log( LOG_DEBUG, "query:  topic is in url AWL");
		}
	} 

	//log( LOG_DEBUG, "query:  qts:%"INT32", gigabits:%"INT32"; "
	//     "maxQTInUrl:%"INT32", maxGbInUrl:%"INT32" AWL",
	//     numQTInUrl, numGigabitsInUrl,
	//     maxQTInUrl, maxGigabitsInUrl );
	return rerankLowerDemotesMore( score, 
				       numQTInUrl+numGigabitsInUrl,
				       maxVal,
				       factor,
				       "pqrqttiu", 
				       "query terms or topics in its url" );
}
*/

// pqrqual
// demote pages that are not high quality
/*
rscore_t PostQueryRerank::rerankQuality ( rscore_t score, 
				      unsigned char quality ) {
	//log( LOG_DEBUG, "query:in PQR::rerankQuality("
	//     "score:%"INT32", quality:%d)"
	//     "[P_factor:%3.3f; P_max:%"INT32"] AWL", 
	//     score, (int)quality, 
	//     m_si->m_cr->m_pqr_demFactQual,
	//     m_si->m_cr->m_pqr_maxValQual );

	float factor = m_si->m_cr->m_pqr_demFactQual;
	if ( factor <= 0 ) return score;
	int32_t maxVal = m_si->m_cr->m_pqr_maxValQual;
	if ( maxVal < 0 ) maxVal = 100;

	return rerankLowerDemotesMore( score, quality, maxVal, factor,
				       "pqrqual", "quality" );
}
*/

// pqrpaths
// demote pages that are not root or have many paths in the url
rscore_t PostQueryRerank::rerankPathsInUrl ( rscore_t score,
					 char *url,
					 int32_t urlLen ) {
	//log( LOG_DEBUG, "query:in PQR::rerankPathsInUrl("
	//     "score:%"INT32", url:%s)"
	//     "[P_factor:%3.3f; P_max:%"INT32"] AWL", 
	//     score, url, 
	//     m_si->m_cr->m_pqr_demFactPaths,
	//     m_si->m_cr->m_pqr_maxValPaths );

	if ( urlLen == 0 ) return score;

	float factor = m_si->m_cr->m_pqr_demFactPaths;
	if ( factor <= 0 ) return score; // disables
	int32_t maxVal = m_si->m_cr->m_pqr_maxValPaths;
	
	// bypass scheme and "://"
	url = strstr( url, "://" );
	if ( ! url ) return score;
	url += 3;

	// count '/'s to get number of paths
	int32_t numPaths = -1; // don't count first path
	for ( url = strchr(url, '/') ; url ; url = strchr(url, '/') ) {
		numPaths++;
		url++;
	}

	return rerankHigherDemotesMore( score, numPaths, maxVal, factor,
					"pqrpaths", "paths in its url" );
}

// pqrcatid
// demote page if does not have a catid
rscore_t PostQueryRerank::rerankNoCatId ( rscore_t score, 
				      int32_t numCatIds,
				      int32_t numIndCatIds ) {
	//log( LOG_DEBUG, "AWL:in PQR::rerankNoCatId("
	//     "score:%"INT32", numCatIds:%"INT32", numIndCatIds:%"INT32")"
	//     "[P_factor:%3.3f]",
	//     score, numCatIds, numIndCatIds,
	//     m_si->m_cr->m_pqr_demFactNoCatId );

	float factor = m_si->m_cr->m_pqr_demFactNoCatId;
	if ( factor <= 0 ) return score; // disables

	if ( numCatIds + numIndCatIds > 0 ) return score;
	
	return rerankAssignPenalty( score, factor, 
				    "pqrcatid", "it has no category id" );
}

// pqrsuper
// demote page if smallest catid has a lot of super topics
rscore_t PostQueryRerank::rerankSmallestCatIdHasSuperTopics ( rscore_t score,
							  Msg20 *msg20 ) {
	//log( LOG_DEBUG, "query:in PQR::rerankSmallestCatIdHasSuperTopics("
	//    "score:%"INT32")"
	//    "[P_factor:%3.3f; P_max:%"INT32"] AWL",
	//    score,
	//    m_si->m_cr->m_pqr_demFactCatidHasSupers,
	//    m_si->m_cr->m_pqr_maxValCatidHasSupers );

	float factor = m_si->m_cr->m_pqr_demFactCatidHasSupers; 
	if ( factor <= 0 ) return score; // disables
	int32_t maxVal = m_si->m_cr->m_pqr_maxValCatidHasSupers;

	// If page doesn't have a catid, we should demote it as if it has
	// max catids, otherwise pages with a catid will be penalized more
	if ( msg20->m_r->size_catIds == 0 ) {
		return rerankAssignPenalty( score, 
					    factor,
					    "pqrsuper", 
					    "it has no category id" );
	}

	// find smallest catid
	int32_t minCatid = 0x7fffffff;//LONG_MAX;
	int32_t numCatids = msg20->m_r->size_catIds / 4;
	for ( int32_t i = 0; i < numCatids; i++ ) {
		if ( msg20->m_r->ptr_catIds[i] < minCatid ) {
			minCatid = msg20->m_r->ptr_catIds[i];
		}
	}
	//log( LOG_DEBUG, "query:  minCatid:%"INT32" AWL", minCatid );

	// count super topics by walking up catids
	int32_t numSupers = -1;
	int32_t currCatId = minCatid;
	int32_t currParentId = minCatid;
	while ( currCatId > 1 ) {
		// next cat
		currCatId = currParentId;
		// get the index for this cat
		int32_t currCatIndex = g_categories->getIndexFromId(currCatId);
		if ( currCatIndex <= 0 ) break;
		// get the parent for this cat
		currParentId = g_categories->m_cats[currCatIndex].m_parentid;
		numSupers++;
	}

	return rerankHigherDemotesMore( score, numSupers, maxVal, factor,
					"pqrsuper", 
					"category ids" );
}

// pqrpgsz
// . demote page based on size. (number of words) The bigger, the 
//   more it should be demoted.
rscore_t PostQueryRerank::rerankPageSize ( rscore_t score,
				       int32_t docLen ) {
	//log( LOG_DEBUG, "query:in PQR::rerankPageSize("
	//     "score:%"INT32", docLen:%"INT32")"
	//     "[P_factor:%3.3f; P_max:%"INT32"] AWL", 
	//     score, docLen, 
	//     m_si->m_cr->m_pqr_demFactPageSize,
	//     m_si->m_cr->m_pqr_maxValPageSize );

	float factor = m_si->m_cr->m_pqr_demFactPageSize;
	if ( factor <= 0 ) return score;
	int32_t maxVal = m_si->m_cr->m_pqr_maxValPageSize;

	// safety check
	if ( docLen <= 0 ) docLen = maxVal;

	return rerankHigherDemotesMore( score, docLen, maxVal, factor,
					"pqrpgsz", "page size" );
}

/*
// pqrloc
const int32_t MIN_PLACEPOP = 50000;
// . returns true if buf contains a location
// . locBuf is the location name
// . locLen is it's length
// . locPop is it's population
bool PostQueryRerank::getLocation( char *locBuf, int32_t locBufLen,
				   int32_t *locLen, int32_t *locPop,
				   char *buf, int32_t bufLen ) {
	//log( LOG_DEBUG, "query:in getLocation(buf:%c%c%c%c, len:%"INT32", "
	//     "uc:%d) AWL",
	//     buf[0], buf[2], buf[4], buf[6], bufLen,

	Words words;
	if ( ! words.set( buf, bufLen, TITLEREC_CURRENT_VERSION,
			  false, // computeIds
			  false  // hasHtmlEntities 
			  ) ) 
		return false;
	
	AppendingWordsWindow ww;
	if ( ! ww.set( &words, 
		       1,   // minWindowSize
		       5,   // maxWindowSize
		       locBufLen,
		       locBuf
		       ) )
		return false;
	
	// find all phrases between length of 1 and 5
	for ( ww.processFirstWindow(); !ww.isDone();  ww.processNextWindow() ){
		ww.act();
		
		char *phrasePtr = ww.getPhrasePtr();
		int32_t  phraseLen = ww.getPhraseLen();
		int32_t  numPhraseWords = ww.getNumWords();
		if ( numPhraseWords == 0 ) continue;
		//log( LOG_DEBUG, "query:  p:%s (%"INT32") AWL",
		//     phrasePtr, phraseLen );

		// see if buf phrase is a place
		int32_t encodeType = csUTF8;//csISOLatin1;
		int32_t placePop = getPlacePop( phrasePtr, phraseLen, 
					     encodeType );
		if ( placePop > MIN_PLACEPOP ) {
			//log( LOG_DEBUG, "query:  p:%s (%"INT32") is "
			//     "loc spec AWL",
			//     phrasePtr, phraseLen );
			*locLen = phraseLen;
			*locPop = placePop;
			return true;
		}

		// check to see if buf phrase's abbreviation is loc spec
		//log( LOG_DEBUG, "query:  utf8 p:%s (%"INT32") AWL",
		//     phrasePtr, phraseLen );
		SynonymInfo synInfo;
		if ( ! g_thesaurus.getSynonymInfo( phrasePtr, 
						   &synInfo,
						   phraseLen ) )
			continue;
		int32_t numSyns = synInfo.m_numSyns;
		for ( int32_t j = 0; j < numSyns; j++ ) {
			char *syn    = synInfo.m_syn[j];
			int32_t  synLen = gbstrlen(syn);
			placePop = getPlacePop( syn, synLen, 
						csISOLatin1 );
			
			if ( placePop > MIN_PLACEPOP ) {
				//log( LOG_DEBUG, "query:   s:%s (%"INT32") is "
				//     "loc spec AWL",
				//     syn, synLen );
				*locLen = phraseLen;
				*locPop = placePop;
				return true;
			}
		}		
	}

	*locLen = 0;
	*locPop = 0;
	return false;
}

// pqrloc
bool PostQueryRerank::preRerankNonLocationSpecificQueries ( ) {
        //log( LOG_DEBUG, "query:in PQR::preRerankNonLocSpecQueries() AWL" );

	if ( m_si->m_pqr_demFactLocTitle <= 0 &&
	     m_si->m_pqr_demFactLocSummary <= 0 &&
	     m_si->m_pqr_demFactLocDmoz <= 0 ) 
		return true;

	//log( LOG_DEBUG, "query: q:%s (%"INT32") AWL",
	//     m_si->m_q->m_orig, 
	//     m_si->m_q->m_origLen );

	// See if query is location specific by building a buffer of 
	// query terms without punct then checking all phrases of that
	// buffer
	int32_t numQWords = m_si->m_q->m_numWords;
	char locBuf[1024];
	int32_t locLen = 0;
	int32_t locPop = 0;
	char buf[MAX_QUERY_LEN];
	char *p = buf;
	Query *q = m_si->m_q;
	for ( int32_t i = 0; i < numQWords; i++ ) {
		QueryWord *qw = &q->m_qwords[i];
		//log( LOG_DEBUG, "query:  qw:%c%c%c%c (%"INT32") "
		//     "inQuotes:%d; inQuoted:%d; quoteStrt:%"INT32" "
		//     "op:%d; opcode:%d; isPunct:%d level:%d; "
		//     "wsign:%d; psign:%d id:%lld "
		//     "ignore:%d AWL",
		//     qw->m_word[0], qw->m_word[2], 
		//     qw->m_word[4], qw->m_word[6],
		//     qw->m_wordLen,
		//     qw->m_inQuotes, qw->m_inQuotedPhrase, qw->m_quoteStart,
		//     qw->m_queryOp, qw->m_opcode, qw->m_isPunct, qw->m_level,
		//     qw->m_wordSign, qw->m_phraseSign, qw->m_wordId,
		//     qw->m_ignoreWord );

		// reset buf if word is punct (except all space) or an opcode
		bool isPunct = qw->m_isPunct;
		bool isAllSpace = false;
		if ( isPunct ) {
			char *s = qw->m_word;
			for ( ; (int)(s-qw->m_word) < qw->m_wordLen; s++ ) {
				if ( ! is_space(*s) ) break;
			}
			isAllSpace = ( s-qw->m_word == qw->m_wordLen );
		}
		if ( (isPunct && ! isAllSpace) || qw->m_opcode != 0 ) {
			// before we reset, see if buffer contains a location
			if ( getLocation( locBuf, 1024, 
					  &locLen, &locPop,
					  buf, p-buf ) ) {
				int32_t encodeType = csUTF8;//csISOLatin1;
				m_querysLoc = hash64d( locBuf, locLen);
				break;
			}
			p = buf;
			//log( LOG_DEBUG, "query:  encountered symbol:%d|%d AWL",
			//     qw->m_isPunct, qw->m_opcode );
			continue;
		}
		// but if word is all space, dont append
		if ( isAllSpace ) continue;

		// skip if word is subtracted out
		if ( qw->m_wordSign == '-' ) continue;

		// skip if word or phrase is under NOT ||| AWL not working right now
		if ( qw->m_queryWordTerm &&
		     qw->m_queryWordTerm->m_underNOT ) continue;
		if ( qw->m_queryPhraseTerm &&
		     qw->m_queryPhraseTerm->m_underNOT ) continue;

		// else, append word + space to buf
		gbmemcpy( p, qw->m_word, qw->m_wordLen );
		p += qw->m_wordLen;
		*p++ = ' ';
	}
	// now see if there's a location in buf
	if ( m_querysLoc == 0 && 
	     getLocation( locBuf, 1024, &locLen, &locPop,
			  buf, p-buf ) ) {
		m_querysLoc = hash64d( locBuf, locLen );
	}
	//log( LOG_DEBUG, "query: q loc:%lld AWL",
	//     m_querysLoc );

	// check the gigabits for locations
	//log( LOG_DEBUG, "query: places lookup gigabits numTopics:%"INT32" AWL",
	//     m_msg40->getNumTopics() );
	m_ignoreLocs.set( 28 );
	// if searching the us, these should not be demoted, so
	// put them into the gigabit table
	if (m_si->m_country == 226) {
		m_ignoreLocs.addKey(hash64d("u.s.",4),true);
		m_ignoreLocs.addKey(hash64d("us",2),true);
		m_ignoreLocs.addKey(hash64d("united states",14),true);
		m_ignoreLocs.addKey(hash64d("u.s.a.",6),true);
		m_ignoreLocs.addKey(hash64d("usa",3),true);
		m_ignoreLocs.addKey(hash64d("america",7),true);
		m_ignoreLocs.addKey(hash64d("american",8),true);
		m_ignoreLocs.addKey(hash64d("americans",9),true);
		m_ignoreLocs.addKey(hash64d("canada",6),true);
		m_ignoreLocs.addKey(hash64d("kanada",6),true);
		m_ignoreLocs.addKey(hash64d("canucks",7),true);
		m_ignoreLocs.addKey(hash64d("canadians",9),true);
		m_ignoreLocs.addKey(hash64d("canadian",8),true);
		m_ignoreLocs.addKey(hash64d("north america",13),true);
		m_ignoreLocs.addKey(hash64d("uk",2),true);
		m_ignoreLocs.addKey(hash64d("united kingdom",14),true);
		m_ignoreLocs.addKey(hash64d("british",7),true);
		m_ignoreLocs.addKey(hash64d("britain",7),true);
		m_ignoreLocs.addKey(hash64d("britons",7),true);
		m_ignoreLocs.addKey(hash64d("great britain",13),true);
	}
	// now add the locations from the gigabits
	int32_t numTopics = m_msg40->getNumTopics();
	for ( int32_t i = 0; !m_si->m_pqr_demInTopics && i < numTopics; i++ ) {
		char *topicStr = m_msg40->getTopicPtr(i);
		int32_t  topicLen = m_msg40->getTopicLen(i);

		Words words;
		if ( ! words.set( topicStr, topicLen, TITLEREC_CURRENT_VERSION,
				  false, // computeIds
				  false  // hasHtmlEntities 
				  ) ) 
			continue;
		
		AppendingWordsWindow ww;
		if ( ! ww.set( &words, 
			       1,   // minWindowSize
			       5,   // maxWindowSize
			       AWW_INIT_BUF_SIZE,
			       NULL
			       ) )
			continue;

		// find all phrases between length of 1 and 5
		for ( ww.processFirstWindow(); 
		      ! ww.isDone(); 
		      ww.processNextWindow() ) {
			ww.act();
			
			char *phrasePtr = ww.getPhrasePtr();
			int32_t  phraseLen = ww.getPhraseLen();
			int32_t  numPhraseWords = ww.getNumWords();
			if ( numPhraseWords == 0 ) continue;
			
			// see if topic phrase is a place
			int32_t placePop = getPlacePop( phrasePtr, phraseLen, 
						     encodeType );
			if ( placePop > MIN_PLACEPOP ) {
				// It's a place, mark it so if a page has
				// this place name in it's title we won't
				// rerank it
				uint64_t h = hash64d( phrasePtr, phraseLen);
				m_ignoreLocs.addKey( h, true );
				//log( LOG_DEBUG, "query:  pre gigabit has "
				//     "location '%s' (%"INT32") [h:%lld] AWL",
				//     phrasePtr, phraseLen, h );
				continue;
			}

			// Check if a gigabit's abbreviation is location 
			// specific 
			SynonymInfo synInfo;
			if ( ! g_thesaurus.getSynonymInfo( phrasePtr, 
							   &synInfo,
							   phraseLen ) )
				continue;
			int32_t numSyns = synInfo.m_numSyns;
			for ( int32_t j = 0; j < numSyns; j++ ) {
				char *syn    = synInfo.m_syn[j];
				int32_t  synLen = gbstrlen(syn);
				placePop = getPlacePop( syn, synLen, 
							csISOLatin1 );
				if ( placePop > MIN_PLACEPOP ) {
					// It's a place, so mark syn
					uint64_t h = hash64d( syn, synLen);
					m_ignoreLocs.addKey( h, true );
					//log( LOG_DEBUG, "query:  pre gigabit"
					//     " has location synonym '%s'"
					//     " h:%lld AWL",
					//     syn, h );
					continue;
				}
			}
		}
	}

	if (m_querysLoc != 0)
		log(LOG_DEBUG, "pqr: query contains a location, "
		    "will not demote location specific results");
	else
		log(LOG_DEBUG, "pqr: query DOES NOT contain a location, "
		    "will demote location specific results");

	return true;
}

// pqrloc
// . if query is not location specific, and a page has a geographic location
//   in its title then demote that page UNLESS the geographic location is 
//   contained in the list of gigabits for the search query. like "Shoes (UK)"
//   or "retail stores in New York" when you are UK and New York are not in 
//   your query. We will need a file of locations. BUT if the location is 
//   contained in the gigabits, do NOT demote such pages, query might have 
//   something like "the big apple" in it... Note: if query ops out of a 
//   location, it should not be considered location specific (like "expo 
//   -montreal"). demote by popularity weight of the place name as returned 
//   from getPlacesPeoplePop().
// . demote results containing geographic locations
//   unless THAT location is in gigabits or in query. fixes
//   'car insurance'? demote a little bit if in summary...
//   or a little bit if in a single dmoz catregory and it is
//   dmoz regional category. do not demote 'united states' 'us'
//   'america' or 'usa' if searching default is the us. do
//   not dmoz dmoz north america:US region if searching in us.
//   but if 'albuquerque' in query, do not demote if 'new mexico'
//   in search results.
rscore_t PostQueryRerank::rerankNonLocationSpecificQueries ( rscore_t score,
							 Msg20 *msg20 ) {
	float titleFactor = m_si->m_pqr_demFactLocTitle;
	float summFactor = m_si->m_pqr_demFactLocSummary;
	float dmozFactor = m_si->m_pqr_demFactLocDmoz;
	if ( titleFactor <= 0 &&
	     summFactor <= 0 &&
	     dmozFactor <= 0 ) 
		return score;
	int32_t maxVal = m_si->m_cr->m_pqr_maxValLoc;
	// if we found a location in the query, don't rerank for locs
	if (m_querysLoc != 0) return score;

	//log( LOG_DEBUG, "query:in PQR::rerankNonLocSpecQueries("
	//     "score:%"INT32")"
	//     "[P_factorTitle:%3.3f; P_factorSummary:%3.3f; P_factorDmoz:%3.3f; "
	//     " P_max:%"INT32"; "
	//     "m_querysLoc:%lld; #m_ignoreLocs:%"INT32"; #summaryLocs:%"INT32"] AWL",
	//     score,
	//     titleFactor, summFactor, dmozFactor,
	//     maxVal, 
	//     m_querysLoc, 
	//     m_ignoreLocs.getNumSlotsUsed(), 
	//     msg20->getNumSummaryLocs() );

	// check if categories are regional and contain a location
	int32_t    numCatids    = msg20->m_r->size_catIds / 4;
	int32_t   *catids       = msg20->m_r->ptr_catIds;
	int32_t    catLocMaxPop = 0;
	uint8_t searchingUS  = (m_si->m_country == 226);
	//log(LOG_DEBUG, "pqr: checking %"INT32" categories for locs AWL",
	//    numCatids);
	for ( unsigned char i = 0; dmozFactor > 0 && i < numCatids; i++ ) {
		SafeBuf sb;
		int32_t catid = catids[i];
		g_categories->printPathFromId(&sb, catid, true);

		// copy first part of category so we can work with it
		const int32_t MAX_PQRCAT = 512;
		char  cat[MAX_PQRCAT];
		int32_t  len = sb.length();
		if (len > MAX_PQRCAT) len = MAX_PQRCAT;
		strncpy(cat, sb.getBufStart(), len);
		cat[len] = '\0';
		//log(LOG_DEBUG, "pqr:  catid:%"INT32" category:'%s' AWL",
		//    catid, cat);

		// see if we have a regional category
		char *p   = cat;
		char  region[64];
		char *q   = region;
		while (*p && q-region < 64 && *p != '/') *q++ = *p++;
		*q = '\0';
		bool catIsRegional = (0 == strncmp(region, "Regional", 8));
		//log(LOG_DEBUG, "pqr:  cat has region:%"INT32" AWL",
		//    (int32_t)catIsRegional);
		// we only care about regional categories
		if (!catIsRegional) continue;

		int32_t placePop = 0;
		// scan category for region
		while (*p) {
			p++;
			q = region;
			while (*p && q-region < 64 && *p != '/') {
				if (*p == '_') *q++ = ' ';
				else           *q++ = *p;
				p++;
			}
			*q = '\0';
			
			bool regionIsUS = (searchingUS &&
					   (0 == strcasecmp(region, "us") ||
					    0 == strcasecmp(region, "united states") ||
					    0 == strcasecmp(region, "usa") ||
					    0 == strcasecmp(region, "america")));
			//log(LOG_DEBUG, "pqr: region:%s (isUS:%"INT32") AWL",
			//    region, (int32_t)regionIsUS);

			// if region is us, skip category demotion
			if (!regionIsUS) {
				// see if region is a place
				placePop = getPlacePop(region, q-region, 
						       csISOLatin1);
				if (placePop > MIN_PLACEPOP) break;
			}
		}
		// if we didn't find a place, go to next cat
		if (placePop <= MIN_PLACEPOP) continue;

		uint64_t h = hash64d( region, q-region );
		if (h == 0) continue;

		// is it the location of the query?
		if (h == m_querysLoc) {
			//log(LOG_DEBUG, "pqr: cat "
			//    "has query's loc "
			//    "[pop:%"INT32"; h:%lld] AWL",
			//    placePop, h);
			return score;
		}
		
		// is it in the gigabits?
		if (m_ignoreLocs.getSlot( h ) != -1) {
			//log(LOG_DEBUG, "pqr: cat has "
			//    "gigabit's loc [pop:%"INT32"; h:%"UINT64"] AWL",
			//    placePop, h);
			return score;
		}
		
		// use only the max pop for all places in category
		if (placePop > catLocMaxPop) {
			//log(LOG_DEBUG, "pqr: cat has a non-query, "
			//    "non-gigabit loc:'%s' %"UINT64" pop:%"INT32" AWL",
			//    region, h, placePop);
			catLocMaxPop = placePop;
			continue;
		}
	}
	//log(LOG_DEBUG, "pqr: categories' max population:%"INT32" AWL", 
	//    catLocMaxPop);
	if (dmozFactor > 0 && catLocMaxPop > MIN_PLACEPOP)
		score = rerankHigherDemotesMore(score, 
						catLocMaxPop, maxVal,
						dmozFactor,
						"pqrlocd", 
						"population of a place in a "
						"category and the place was "
						"not in the query or gigabits");


	// check if summary contains a location
	// check if summary's location is in gigabits
	int32_t      numSummaryLocs   = msg20->m_r->size_summLocs/8;
	uint64_t *summaryLocs      = msg20->m_r->ptr_summLocs;
	int32_t     *summaryLocsPops  = msg20->m_r->ptr_summLocsPop;
	int32_t      summaryLocMaxPop = 0;
	for (int32_t i = 0; summFactor > 0 && i < numSummaryLocs; i++) {
		uint64_t h        = summaryLocs[i];
		int32_t     placePop = summaryLocsPops[i];
		if (h == 0) continue;
		if (placePop <= MIN_PLACEPOP) continue;
		
		// is it the location of the query?
		if ( h == m_querysLoc ) {
			//log( LOG_DEBUG, "pqr: summary "
			//     "has query's loc "
			//     "[pop:%"INT32"; h:%lld] AWL",
			//     placePop, h );
			return score;
		}

		// is it in the gigabits?
		if (m_ignoreLocs.getSlot( h ) != -1 ) {
			//log( LOG_DEBUG, "pqr: summary has "
			//     "gigabit's loc [pop:%"INT32"; h:%"UINT64"] AWL",
			//     placePop, h );
			return score;
		}

		// use only the max pop for all places in title
		if ( placePop > summaryLocMaxPop ) {
			//log( LOG_DEBUG, "pqr: summary has a non-query, "
			//     "non-gigabit loc:%"UINT64" pop:%"INT32" AWL",
			//     h, placePop );
			summaryLocMaxPop = placePop;
			continue;
		}
	}
	//log( LOG_DEBUG, "pqr: summary's max population:%"INT32" AWL", 
	//     summaryLocMaxPop );
	if (summFactor > 0 && summaryLocMaxPop > MIN_PLACEPOP)
		score = rerankHigherDemotesMore(score, 
						summaryLocMaxPop, maxVal,
						summFactor,
						"pqrlocs", 
						"population of a place in its "
						"summary and the place was "
						"not in the query or gigabits");

	// check if title contains a location 
	if (titleFactor <= 0) return score;
	char *pageTitle    = msg20->getTitle();
	int32_t  pageTitleLen = msg20->getTitleLen(); 
	Words words;
	if ( ! words.set( pageTitle, pageTitleLen, TITLEREC_CURRENT_VERSION,
			  false, // computeIds
			  false  // hasHtmlEntities 
			  ) ) 
		return score;

	AppendingWordsWindow ww;
	if ( ! ww.set( &words, 
		       1,   // minWindowSize
		       5,   // maxWindowSize 
		       AWW_INIT_BUF_SIZE,
		       NULL
		       ) )
		return score;

	// find all phrases between length of 1 and 5
	int32_t titleLocMaxPop = 0;
	for ( ww.processFirstWindow(); ! ww.isDone(); ww.processNextWindow()) {
		ww.act();

		char *phrasePtr = ww.getPhrasePtr();
		int32_t  phraseLen = ww.getPhraseLen();
		int32_t  numPhraseWords = ww.getNumWords();
		if ( numPhraseWords == 0 ) continue;

		// Get the place's population
		// If it's a place, check gigabits for the place name
		int32_t encodeType = csUTF8; //ISOLatin1;
		int32_t placePop = getPlacePop( phrasePtr, phraseLen, 
					     encodeType );
		if ( placePop > MIN_PLACEPOP ) {
			// Check if place is same as query
			// Check if gigabits has this location or 
			// an abbreviation of the location, if so don't 
			// rerank this page
			uint64_t h = hash64d(phrasePtr, phraseLen);
			if ( h == 0 ) continue;

			// is it the query's location?
			if ( h == m_querysLoc ) {
				//log( LOG_DEBUG, "query:  title has "
				//     "query's loc [pop:%"INT32"; h:%"UINT64"] AWL",
				//     placePop, h );
				return score;
			}

			// is it in the gigabits?
			if ( m_ignoreLocs.getSlot( h ) != -1 ) {
				//log( LOG_DEBUG, "pqr:  title has "
				//     "gigabit's loc [pop:%"INT32"; h:%"UINT64"] AWL",
				//     placePop, h );
				return score;
			}
		}
		// use only the max pop for all places in title
		if ( placePop > titleLocMaxPop ) {
			//log( LOG_DEBUG, "pqr:  title has a non-query, "
			//     "non-gigabit loc:'%s' (%"INT32") pop:%"INT32" AWL",
			//     phrasePtr, phraseLen, placePop );
			titleLocMaxPop = placePop;
			continue;
		}
		
		// If we haven't found a place name yet, check for 
		// abbreviations of a place name
		//log( LOG_DEBUG, "pqr:  phrase:'%s' (%"INT32") words:%"INT32" "
		//     "pop:%"INT32" AWL", 
		//     phrasePtr, phraseLen, numPhraseWords, 
		//     placePop );
		SynonymInfo synInfo;
		if ( ! g_thesaurus.getSynonymInfo( phrasePtr, &synInfo,
						   phraseLen ) ) {
			continue;
		}
		int32_t numSyns = synInfo.m_numSyns;
		for ( int32_t j = 0; j < numSyns; j++ ) {
			char *syn    = synInfo.m_syn[j];
			int32_t  synLen = gbstrlen(synInfo.m_syn[j]);
			placePop = getPlacePop( syn, synLen, 
						csISOLatin1 );
			if ( placePop > MIN_PLACEPOP ) {
				// Check if gigabits has an abbreviation 
				// of the location, if so don't rerank 
				// this page
				uint64_t h = hash64d(syn, synLen);
				if ( h == 0 ) continue;

				// is syn the query's loc?
				if ( h == m_querysLoc ) {
					//log( LOG_DEBUG, "pqr:  title "
					//     "has query's loc syn "
					//     "[pop:%"INT32"; h:%lld] AWL",
					//     placePop, h );
					return score;
				}

				// is syn in gigabits?
				if ( m_ignoreLocs.getSlot( h ) != -1 ) {
					//log(LOG_DEBUG, "pqr:  syn title "
					//    " has gigabits's loc '%s' "
					//    "[pop:%"INT32"; h:%lld] AWL",
					//    syn,
					//    placePop, h );
					return score;
				}

				// only use max pop in calculations
				if ( placePop > titleLocMaxPop ) {
					//log( LOG_DEBUG, "pqr:  title "
					//     "has a non-query, "
					//     "non-gigabit loc syn AWL" );
					titleLocMaxPop = placePop;
				}
			}
		}
	}
	//log( LOG_DEBUG, "pqr: title's max population:%"INT32" AWL", 
	//     titleLocMaxPop );

	return rerankHigherDemotesMore( score, titleLocMaxPop, maxVal, 
					titleFactor,
					"pqrloct", 
					"population of a place in its title "
					"and the place was not in the query "
					"or gigabits" );
}
*/
// pqrhtml, pqrxml
// demote if content type is not html (or is xml)
/*
rscore_t PostQueryRerank::rerankContentType ( rscore_t score,
					  char contentType ) {
	float htmlFactor = m_si->m_cr->m_pqr_demFactNonHtml;
	float xmlFactor  = m_si->m_cr->m_pqr_demFactXml;

 	//log( LOG_DEBUG, "query:in PQR::rerankContentType("
	 //    "score:%"INT32", content-type:%"INT32")"
	 //    "[P_factorHtml:%3.3f; P_factorXml:%3.3f] AWL",
	 //    score, (int32_t)contentType, 
	 //    htmlFactor, xmlFactor );

	// if completely disabled or page is html, don't do anything
	if ( xmlFactor <= 0 && htmlFactor <= 0 || contentType == CT_HTML )
		return score;

	// if demoting for xml, then do that
	if ( xmlFactor > 0 && contentType == CT_XML )
		return rerankAssignPenalty( score, xmlFactor,
					    "pqrxml", "it is xml" );

	// we are demoting for non-html and the page is not html
	return rerankAssignPenalty( score, htmlFactor,
				    "pqrhtml", "it is not html" );
}
*/

// pqrfsd
// setup 
bool PostQueryRerank::preRerankOtherPagesFromSameHost( Url *pageUrl ) {
	// don't do anything if this method is disabled
	if ( m_si->m_cr->m_pqr_demFactOthFromHost <= 0 ) return true;

	// don't add if no url
	if ( pageUrl->getUrlLen() == 0 ) return true;

	//log( LOG_DEBUG, "query:in PQR::preRerankOtherPagesFromSameHost() AWL");
	//log( LOG_DEBUG, "query: u:'%s' host:'%s' (%"INT32"); "
	//     "domain:'%s' (%"INT32") AWL",
	//     pageUrl->m_url,
	//     pageUrl->getHost(), pageUrl->getHostLen(),
	//     pageUrl->getDomain(), pageUrl->getDomainLen() );
	char *host = pageUrl->getDomain();
	int32_t  hostLen = pageUrl->getDomainLen();
	uint64_t key = hash64Lower_a( host, hostLen );
	if ( key == 0 ) key = 1;
	int32_t slot = m_hostCntTable.getSlot( key );
	if ( slot == -1 ) {
		m_hostCntTable.addKey( key, 0 ); // first page doesn't cnt
	}
	else {
		int32_t *cnt = m_hostCntTable.getValuePointerFromSlot( slot );
		(*cnt)++;
	}

	return true;
}

// pqrfsd
// . if page does not have any other pages from its same hostname in the
//   search results (clustered or not) then demote it. demote based on 
//   how many pages occur in the results from the same hostname. (tends 
//   to promote pages from hostnames that occur a lot in the unclustered 
//   results, they tend to be authorities) If it has pages from the same
//   hostname, they must have the query terms in different contexts, so 
//   we must get the summaries for 5 of the results, and just cluster the rest.
rscore_t PostQueryRerank::rerankOtherPagesFromSameHost ( rscore_t score,
							  Url *pageUrl ) {
	//log( LOG_DEBUG, "query:in PQR::rerankOtherPagesFromSameHost("
	//     "score:%"INT32", url:'%s', urlLen:%"INT32")"
	//     "[P_factor:%3.3f; P_max:%"INT32"] AWL",
	//     score, pageUrl->getUrl(), pageUrl->getUrlLen(),
	//     m_si->m_cr->m_pqr_demFactOthFromHost,
	//     m_si->m_cr->m_pqr_maxValOthFromHost );

	if ( pageUrl->getUrlLen() == 0 ) return score;

	float factor = m_si->m_cr->m_pqr_demFactOthFromHost;
	if ( factor <= 0 ) return score; // disables
	int32_t maxVal = m_si->m_cr->m_pqr_maxValOthFromHost;
	if ( maxVal < 0 ) maxVal = m_numToSort-1; // all but this one

	// . lookup host for this page in hash table to get number of other 
	//   pages from the same host
	char *host = pageUrl->getDomain(); 
	int32_t  hostLen = pageUrl->getDomainLen();
	uint64_t key = hash64Lower_a( host, hostLen );
	int32_t slot = m_hostCntTable.getSlot( key );
	int32_t numFromSameHost = m_hostCntTable.getValueFromSlot( slot );
	
	//log( LOG_DEBUG, "query:  numFromSameHost:%"INT32" AWL", numFromSameHost );

	return rerankLowerDemotesMore( score, 
				       numFromSameHost, maxVal, 
				       factor,
				       "pqrfsd", 
				       "other pages from the same host" );
}

// pqrctid
// . if page is from a topic in dmoz that is in common with a lot of other
//   results, then do not demote it as much as if it is not. ("birds of 
//   a feather") Reduce demotion penalty as you demote each result in 
//   order to avoid "clumping".
// setup	
bool PostQueryRerank::preRerankCommonTopicsInDmoz( Msg20Reply *mr ) {
	if ( m_si->m_cr->m_pqr_demFactComTopicInDmoz <= 0 ) return true;
	//GBASSERT( msg20 );
	if ( ! mr ) { char *xx=NULL;*xx=0; }

	//log( LOG_DEBUG, "query:in PQR::preRerankCommonTopicsInDmoz() "
	//     "AWL" );
	//log(LOG_DEBUG, "query:  qdmoz pre cnt:%d AWL", 
	//    (int)msg20->m_numCatids);
	int32_t numCatids = mr->size_catIds/4;//msg20->getNumCatids();
	for ( unsigned char i = 0; i < numCatids; i++ ) {
		int32_t key = mr->ptr_catIds[i];//msg20->getDmozCatids()[i];
		if ( key == 0 ) key = 1;
		int32_t slot = m_dmozTable.getSlot( key );
		//log( LOG_DEBUG, "query:  qdmoz pre %"INT32"/%"INT32"; "
		//     "catId:%"INT32"; slot:%"INT32" AWL", 
		//     (int32_t)i+1, (int32_t)msg20->m_numCatids, 
		//     key, slot );
		if ( slot == -1 ) {
			// first occurance
			// cnt is 0, no other common topics
			// demotion factor is the parm
			ComTopInDmozRec rec;
			rec.cnt = 0;
			rec.demFact =
				m_si->m_cr->m_pqr_demFactComTopicInDmoz;
			m_dmozTable.addKey( key, rec );
			//log(LOG_DEBUG, "query:  qdmoz pre occurance 1 AWL");
		}
		else {
			// nth occurance
			ComTopInDmozRec *rec =
				m_dmozTable.getValuePointerFromSlot( slot );
			rec->cnt++;
			//log( LOG_DEBUG, "query:  qdmoz pre key:%"INT32" "
			//     "occurance %"INT32" AWL", 
			//     key, rec->cnt );
		}
	}
	return true;
}

// pqrctid
// . if page is from a topic in dmoz that is in common with a lot of other
//   results, then do not demote it as much as if it is not. ("birds of 
//   a feather") Reduce demotion penalty as you demote each result in 
//   order to avoid "clumping".
rscore_t PostQueryRerank::rerankCommonTopicsInDmoz ( rscore_t score,
						 Msg20 *msg20 ) {
	//log( LOG_DEBUG, "query:in PQR::rerankCommonTopicsInDmoz("
	//     "score:%"INT32")"
	//     "[P_max:%"INT32" P_decFact:%3.3f] AWL",
	//     score,
	//     m_si->m_cr->m_pqr_maxValComTopicInDmoz,
	//     m_si->m_cr->m_pqr_decFactComTopicInDmoz );

	//log( LOG_DEBUG, "query:  qdmoz cnt:%"INT32" AWL", 
	//     (int32_t)msg20->m_numCatids );

	if ( m_si->m_cr->m_pqr_demFactComTopicInDmoz <= 0 )
		return score;
	int32_t maxVal = m_si->m_cr->m_pqr_maxValComTopicInDmoz;
	if ( maxVal < 0 ) maxVal = m_numToSort;

	// . see if page is from a topic in dmoz that is in common with a 
	//   lot of other results
	// . if no catid, result will be not be demoted 
	float chosenDemFact = 0.0;
	int32_t numComTopicsInDmoz = 0;
	int32_t maxComTopicsInDmoz = 0;
	int32_t numCatids = msg20->m_r->size_catIds/4;//getNumCatids();
	for ( unsigned char i = 0; i < numCatids; i++ ) {
		int32_t key = msg20->m_r->ptr_catIds[i];//getDmozCatids()[i];
		int32_t slot = m_dmozTable.getSlot( key );
		ComTopInDmozRec *rec = 
			m_dmozTable.getValuePointerFromSlot( slot ); 
		//log( LOG_DEBUG, "query:  slot:%"INT32" key:%"INT32" cnt:%"INT32"; "
		//     "demFact:%3.3f AWL",
		//     slot, key, 
		//     rec->cnt, rec->demFact );
		
		// add # of other pages with same topic as this
		numComTopicsInDmoz += rec->cnt;

		// . find the slot with the max common topics in dmoz so
		//   it can be decayed
		if ( rec->cnt > maxComTopicsInDmoz ) {
			chosenDemFact = rec->demFact;
			maxComTopicsInDmoz = rec->cnt;
		}
	}

	score = rerankHigherDemotesMore( score, 
					 numComTopicsInDmoz, maxVal, 
					 chosenDemFact,
					 "pqrctid",
					 "common topics in dmoz "
					 "as other results" );

	// now decay the factors
	float decFactor = 
		m_si->m_cr->m_pqr_decFactComTopicInDmoz;
	if ( decFactor <  0 ) return score; 
	for ( unsigned char i = 0; i < numCatids; i++ ) {
		int32_t key = msg20->m_r->ptr_catIds[i];
		int32_t slot = m_dmozTable.getSlot( key );
		ComTopInDmozRec *rec = 
			m_dmozTable.getValuePointerFromSlot( slot ); 
		rec->demFact *= (1.0 - decFactor);
		//log( LOG_DEBUG, "query:  decay slot:%"INT32" key:%"INT32" "
		//     "cnt:%"INT32"; decFact:%3.3f; new demFact:%3.3f AWL",
		//     slot, key, 
		//     rec->cnt, decFactor, rec->demFact );
	}

	return score;
}

// pqrdcndcqt
// . if the dmoz category names contain a query term (or its synonyms or 
//   gigabits), "boost" the result based on the query term weight (look at 
//   query phrase term weights, too) (actually, demote others that do not 
//   have them...)
/*
rscore_t PostQueryRerank::rerankDmozCategoryNamesDontHaveQT ( rscore_t score,
							  Msg20 *msg20 ) {
	//log( LOG_DEBUG, "query:in PQR::rerankDmozCategoryNamesDontHaveQT("
	//     "score:%"INT32")"
	//     "[P_factor:%3.3f; P_max:%"INT32"] AWL",
	//     score,
	//     m_si->m_cr->m_pqr_demFactDmozCatNmNoQT,
	//     m_si->m_cr->m_pqr_maxValDmozCatNmNoQT );

	float factor = m_si->m_cr->m_pqr_demFactDmozCatNmNoQT;
	if ( factor <= 0 ) return score; // disables
	int32_t maxVal = m_si->m_cr->m_pqr_maxValDmozCatNmNoQT;

        int32_t numQTsInDmoz = 0;
	char *pd = msg20->m_r->ptr_dmozTitles;
	int32_t numCatids = msg20->m_r->size_catIds/4;
	int32_t numQTs = m_si->m_q->m_numTerms;
	HashTableT<uint64_t, int32_t> matchedIds;
	matchedIds.set( numQTs*2 );
	for ( int32_t j = 0; j < numCatids; j++ ) {
		char *currTitle = pd;
		int32_t currTitleLen = gbstrlen(pd);
		if ( currTitleLen == 0 ) continue;
		//log( LOG_DEBUG, "query: currTitle:%s (%"INT32") AWL", 
		//     currTitle, currTitleLen );

		Words w;
		Bits b;
		Phrases p;
		int64_t *wids;
		int32_t       nw;
		int64_t *pids;
		if ( ! w.set( currTitle     ,
			      currTitleLen  ,
			      TITLEREC_CURRENT_VERSION,
			      true          , // computeIds
			      false         ) )
			goto next;
		if ( ! b.set( &w, TITLEREC_CURRENT_VERSION ,0) )
			goto next;
		if ( ! p.set( &w            , 
			      &b            ,
			      true          , // useStopWords
			      false         , // useStems
			      TITLEREC_CURRENT_VERSION,
			      0             ) ) // niceness
			goto next;

		wids = w.getWordIds   ();
		nw   = w.getNumWords  ();
		pids = p.getPhraseIds2 ();
		// go through all words in cat name
		for ( int32_t i = 0; i < nw; i++ ) {
			// go through all query terms
			for ( int32_t k = 0; k < numQTs; k++ ) {
				QueryTerm *qt = 
					&m_si->m_q->m_qterms[k];
				int64_t rawTermId = qt->m_rawTermId;
				// ignore 0 termIds
				if ( rawTermId == 0 ) continue;
				// see if we already matched this id
				int32_t n = matchedIds.getSlot( rawTermId );
				if ( n != -1 ) continue;

				// compare this query term to cat word
				if ( rawTermId == wids[i] ) {
					matchedIds.addKey( rawTermId, 0 );
					numQTsInDmoz++;
					//log( LOG_DEBUG, "query: qt-dmozw "
					//     "match '%s' (%"INT32") AWL",
					//     qt->m_term, 
					//     qt->m_termLen );
					continue;
				}
				// compare this query term to cat phrase
				if ( qt->m_isPhrase && rawTermId == pids[i] ) {
					matchedIds.addKey( rawTermId, 0 );
					numQTsInDmoz++;
					//log( LOG_DEBUG, "query: qt-dmozp "
					//     "match '%s' (%"INT32") AWL",
					//     qt->m_term, 
					//     qt->m_termLen );
					continue;
				}

				// if we haven't matched yet, check syns
				SynonymInfo synInfo;
			        if ( !  g_thesaurus.getSynonymInfo( rawTermId, 
								    &synInfo )) 
					continue;
				int32_t numSyns = synInfo.m_numSyns;
				for ( int32_t k = 0; k < numSyns; k++ ) {
					//log( LOG_DEBUG, "query: syn:'%s' AWL", 
					//     synInfo.m_syn[j]);
					uint64_t h ;
					h =hash64Lower_utf8(synInfo.m_syn[j],
							    gbstrlen(synInfo.m_syn[j]));
					// see if we already matched this id
					int32_t n = matchedIds.getSlot( h );
					if ( n != -1 ) continue;
					// Compare this query term syn to 
					// cat word
					if ( (int64_t)h == wids[i] ) {
						matchedIds.addKey( h, 0 );
						numQTsInDmoz++;
						//log( LOG_DEBUG, "query: "
						//     "synmatch:'%s' "
						//     "in dmozw:'%s' AWL", 
						//     synInfo.m_syn[j], 
						//     currTitle );
						continue;
					}
					// Compare this query term syn to 
					// cat phrase
					if ( qt->m_isPhrase && 
					     (int64_t)h == pids[i] ) {
						matchedIds.addKey( h, 0 );
						numQTsInDmoz++;
						//log( LOG_DEBUG, "query: "
						//     "synmatch:'%s' "
						//     "in dmozp:'%s' AWL", 
						//     synInfo.m_syn[j], 
						//     currTitle );
						continue;
					}
				}
			}
		}
next:
		pd += currTitleLen;
	}
	//log( LOG_DEBUG, "query: qts or syns in dmoz cat name:%"INT32" AWL", 
	//     numQTsInDmoz );

	return rerankLowerDemotesMore( score, 
				       numQTsInDmoz, maxVal, 
				       factor,
				       "pqrdcndcqt",
				       "query terms in its dmoz category names");
}
*/

// pqrdcndcgb
// . if the dmoz category names contain a query term (or its synonyms or 
//   gigabits), "boost" the result based on the query term weight (look at 
//   query phrase term weights, too) (actually, demote others that do not 
//   have them...)
/*
rscore_t PostQueryRerank::rerankDmozCategoryNamesDontHaveGigabits ( rscore_t score,
								Msg20 *msg20 ) {
	//log( LOG_DEBUG, "query:in PQR::rerankDmozCategoryNamesDontHaveGigabits("
	//     "score:%"INT32")"
	//     "[P_factor:%3.3f; P_max:%"INT32"] AWL",
	//     score,
	//     m_si->m_cr->m_pqr_demFactDmozCatNmNoGigabits,
	//     m_si->m_cr->m_pqr_maxValDmozCatNmNoGigabits );

	float factor = m_si->m_cr->m_pqr_demFactDmozCatNmNoGigabits;
	if ( factor <= 0 ) return score; // disables
	int32_t maxVal = m_si->m_cr->m_pqr_maxValDmozCatNmNoGigabits;
	if ( maxVal < 0 ) maxVal = m_si->m_docsToScanForTopics;

	// find number of gigabits in dmoz category name
	int32_t numGigabitsInDmoz = 0;
	// go through gigabits each possible phrase in gigabits
	//log( LOG_DEBUG, "query: numGigabits:%"INT32" AWL", 
	//     m_msg40->getNumTopics() );
	int32_t numTopics = m_msg40->getNumTopics();
	HashTableT<int64_t, int32_t> matchedIds;
	matchedIds.set( numTopics*4 );
	for ( int32_t i = 0; i < numTopics; i++ ) {
		Words words;
		if ( ! words.set( m_msg40->getTopicPtr(i), 
				  m_msg40->getTopicLen(i), 
				  TITLEREC_CURRENT_VERSION,
				  false,  // computeIds
				  false  // hasHtmlEntities 
				  ) ) 
			continue;
		
		AppendingWordsWindow ww;
		if ( ! ww.set( &words, 
			       1,   // minWindowSize
			       4,   // maxWindowSize 
			       AWW_INIT_BUF_SIZE,
			       NULL
			       ) )
			continue;

		// find all phrases between length of 1 and 4
		for ( ww.processFirstWindow(); 
		      ! ww.isDone(); 
		      ww.processNextWindow() ) {
			ww.act();
			
			char *phrasePtr = ww.getPhrasePtr();
			int32_t  phraseLen = ww.getPhraseLen();
			int32_t  numPhraseWords = ww.getNumWords();
			if ( numPhraseWords == 0 ) continue;
			//log( LOG_DEBUG, "query: gb phrase:%s (%"INT32") AWL",
			//     phrasePtr, phraseLen );
			// see if we already matched this phrase
			uint64_t h = hash64Lower_utf8( phrasePtr, phraseLen );
			if ( h == 0 ) h = 1;
			if ( matchedIds.getSlot( h ) != -1 )
				continue;
			// ignore phrases that are just common words
			if ( isCommonWord( h ) ) 
				continue;
			matchedIds.addKey( h, 0 );
			
			// go through dmoz category names
			char *p = msg20->m_r->ptr_dmozTitles;
			int32_t numCatids = msg20->m_r->size_catIds/4;
			for ( int32_t j = 0; j < numCatids; j++ ) {
				char *currTitle = p;
				int32_t currTitleLen = gbstrlen(p);
				if ( currTitleLen == 0 ) continue;
								
				//log( LOG_DEBUG, "query:   dmoz:%s (%"INT32") AWL",
				//     currTitle, currTitleLen );

				// check if gigabit is in dmoz category name 
				if (strncasestr(currTitle, phrasePtr, 
						currTitleLen, phraseLen)){
					//log( LOG_DEBUG, "query:    gb is in "
					//     "dmoz AWL");
					numGigabitsInDmoz++;
				}
				p += currTitleLen;
			}
		}
	}
	//log( LOG_DEBUG, "query:  numGigabitsInDmoz:%"INT32" AWL", 
	//     numGigabitsInDmoz );

	return rerankLowerDemotesMore( score, 
				       numGigabitsInDmoz, maxVal, 
				       factor,
				       "pqrdcndcgb",
				       "gigabits in its dmoz category names" );	
}
*/

// pqrdate
// . demote pages by datedb date
rscore_t PostQueryRerank::rerankDatedbDate( rscore_t score,
					time_t datedbDate ) {
	float factor = m_si->m_cr->m_pqr_demFactDatedbDate;
	if ( factor <= 0 ) return score;
	int32_t minVal = m_si->m_cr->m_pqr_minValDatedbDate;
	if ( minVal <= 0 ) minVal = 0;
	minVal *= 1000;
	int32_t maxVal = m_si->m_cr->m_pqr_maxValDatedbDate;
	if ( maxVal <= 0 ) maxVal = 0;
	maxVal = m_now - maxVal*1000;

	//log( LOG_DEBUG, "query:in PQR::rerankDatedbDate("
	//     "score:%"INT32", datedbDate:%"INT32")"
	//     "[P_factor:%3.3f; maxVal:%"INT32"] AWL",
	//     score, datedbDate,
	//     factor, maxVal );

	// don't penalize results whose publish date is unknown
	if ( datedbDate == -1 ) return score;
	if ( datedbDate <= minVal ) 
		return rerankAssignPenalty( score,
					    factor,
					    "pqrdate",
					    "publish date is older then "
					    "minimum value" );

	return rerankLowerDemotesMore( score,
				       datedbDate-minVal, maxVal-minVal,
				       factor,
				       "pqrdate",
				       "publish date" );
}

// pqrprox
// . demote pages by the average distance of query terms from
// . one another in the document.  Lower score is better.
/*
rscore_t PostQueryRerank::rerankProximity( rscore_t score,
				       float proximityScore,
				       float maxScore) {
	// . a -1 implies did not have any query terms
	// . see Summary.cpp proximity algo
	if ( proximityScore == -1 ) return 0;
	if(m_si->m_pqr_demFactProximity <= 0) return score;
	float factor = (// 1 -
			(proximityScore/maxScore)) *
		m_si->m_pqr_demFactProximity;
	if ( factor <= 0 ) return score;
	//return rerankAssignPenalty(score, 
	//			   factor,
	//			   "pqrprox",
	//			   "proximity rerank");
	// just divide the score by the proximityScore now

	// ...new stuff...
	if ( proximityScore == 0.0 ) return score;
	float score2 = (float)score;
	score2 /= proximityScore;
	score2 += 0.5;
	rscore_t newScore = (rscore_t)score2;
	if(m_si->m_debug || g_conf.m_logDebugPQR )
		logf( LOG_DEBUG, "query: pqr: result demoted "
		      "from %.02f to %.02f becaose of proximity rerank",
		      (float)score,(float)newScore);
	return newScore;
}
*/

// pqrinsec
// . demote pages by the average of the score of the sections
// . in which the query terms appear in.  Higher score is better.
rscore_t PostQueryRerank::rerankInSection( rscore_t score,
				       int32_t summaryScore,
				       float maxScore) {
	if(m_si->m_pqr_demFactInSection <= 0) return score;
	float factor = ( 1 -
			 (summaryScore/maxScore)) *
		m_si->m_pqr_demFactInSection;
	if ( factor <= 0 ) return score;
 	return rerankAssignPenalty(score, 
 				   factor,
 				   "pqrsection",
 				   "section rerank");
}


/*
rscore_t PostQueryRerank::rerankSubPhrase( rscore_t score,
				       float diversity,
				       float maxDiversity) {
	if(maxDiversity == 0) return score;
	float factor = (1 - (diversity/maxDiversity)) *
		m_si->m_pqr_demFactSubPhrase;
	if ( factor <= 0 ) return score;
	return rerankAssignPenalty(score, 
				   factor,
				   "pqrspd",
				   "subphrase demotion");

}
*/

bool PostQueryRerank::attemptToCluster ( ) {
	// find results that should be clustered
	bool                       needResort   = false;
	HashTableT<uint32_t, int32_t> hostPosTable;
	hostPosTable.set(m_numToSort);
	for (int32_t i = 0; i < m_numToSort; i++) {
		// look up this hostname to see if it's been clustered
		uint32_t key     = m_m20List[i].m_host;
		if ( key == 0 ) key = 1;
		int32_t     slot    = hostPosTable.getSlot(key);
		if (slot != -1) {
			// see if we are within 10 results of first result
			// from same host
			int32_t firstPos = hostPosTable.getValueFromSlot(slot);
			if (i - firstPos > 1 && i - firstPos < 10) {
				// this result can be clustered
				rscore_t maxNewScore;
				maxNewScore = m_m20List[firstPos].m_score;
				if (maxNewScore <= m_m20List[i].m_score)
					continue;
				needResort = true;
				if(m_si->m_debug||g_conf.m_logDebugPQR )
					logf(LOG_DEBUG, "pqr: re-ranking result "
					     "%"INT32" (%s) from score %.02f to "
					     "score %.02f "
					     "in order to cluster it with "
					     "result "
					     "%"INT32" (%s)",
					     i, 
					     m_m20List[i].m_m20->m_r->ptr_ubuf,
					     (float)m_m20List[i].m_score, 
					     (float)maxNewScore,
					     firstPos, 
					     m_m20List[firstPos].m_m20->m_r->ptr_ubuf);
				// bump up the score to cluster this result
				m_m20List[i].m_score = maxNewScore;
			}
			else {
				hostPosTable.setValue(slot, i);
			}
		}
		else {
			// add the hostname of this result to the table
			if (!hostPosTable.addKey(key, i)) {
				g_errno = ENOMEM;
				return false;
			}
		}
	}

	// re-sort the array if necessary
	if (needResort) {
		log(LOG_DEBUG, "pqr: re-sorting results for clustering");
		gbmergesort( (void *) m_m20List, (size_t) m_numToSort, 
			     (size_t) sizeof(M20List),
			     (int (*)(const void *, const void *))s_reSortFunction);
	}

	return true;
}

// Sort function for post query reranking's M20List
static int32_t s_firstSortFunction(const M20List * a, const M20List * b)
{
	// Sort by tier first, then score
	// When sorting by tier, an explicit match (0x40) in a higher tier 
	// gets precedence over an implicit match (0x20) from a lower tier
	// Note: don't sort by tier, don't consider bitscores
	//if ( a->tier < b->tier && 
	//    (a->bitScore & 0x40 || !b->bitScore & 0x40) ) 
	//	return -1; 
	//if ( a->tier > b->tier && 
	//    (b->bitScore & 0x40 || !a->bitScore & 0x40) )
	//	return 1; 

	// Absolute match proximity
	//if ( a->m20->m_proximityScore > b->m20->proximityScore )
	//	return -1;
	//else if ( a->m20->m_proximityScore < b->m20->proximityScore )
	//	return 1;

	// same tier, same proximity, sort by score
	if ( a->m_score > b->m_score ) 
		return -1;
	if ( a->m_score < b->m_score ) 
		return 1;

	// same tier, same proximity, same score, sort by docid
	//if ( a->docId < b->docId )
	//	return -1;
	//if ( a->docId > b->docId )
	//	return 1;

	// same score, sort by host
	if ( a->m_host > b->m_host )
		return -1;
	if ( a->m_host < b->m_host )
		return 1;

	return 0;
}

// Sort function for post query reranking's M20List
static int32_t s_reSortFunction(const M20List * a, const M20List * b)
{
	// Sort by tier first, then score
	// When sorting by tier, an explicit match (0x40) in a higher tier 
	// gets precedence over an implicit match (0x20) from a lower tier
	// Note: don't sort by tier, don't consider bitscores
	//if ( a->tier < b->tier && 
	//    (a->bitScore & 0x40 || !b->bitScore & 0x40) ) 
	//	return -1; 
	//if ( a->tier > b->tier && 
	//    (b->bitScore & 0x40 || !a->bitScore & 0x40) )
	//	return 1; 

	// Absolute match proximity
	//if ( a->m20->m_proximityScore > b->m20->proximityScore )
	//	return -1;
	//else if ( a->m20->m_proximityScore < b->m20->proximityScore )
	//	return 1;

	// same tier, same proximity, sort by score
	if ( a->m_score > b->m_score ) 
		return -1;
	if ( a->m_score < b->m_score ) 
		return 1;

	// same tier, same proximity, same score, sort by docid
	//if ( a->docId < b->docId )
	//	return -1;
	//if ( a->docId > b->docId )
	//	return 1;

	// same score, sort by host
	if ( a->m_host > b->m_host )
		return -1;
	if ( a->m_host < b->m_host )
		return 1;

	return 0;
}

#ifdef DEBUGGING_LANGUAGE
// Debug stuff, remove before flight
static void DoDump(char *loc, Msg20 **m20, int32_t num, 
		   score_t *scores, char *tiers) {
	int x;
	char *url;
	//log(LOG_DEBUG, "query: DoDump(): checkpoint %s AWL DEBUG", loc);
	for(x = 0; x < num; x++) {
		url = m20[x]->getUrl();
		if(!url) url = "None";
		//log( LOG_DEBUG, "query: DoDump(%d): "
		//     "tier:%d score:%"INT32" [url:'%s'] msg20:%p\n AWL DEBUG",
		//     x, tiers[x], scores[x], url, m20[x] );
	}
}
#endif // DEBUGGING_LANGUAGE

