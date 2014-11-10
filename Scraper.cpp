#include "gb-include.h"

#include "Scraper.h"
//#include "CollectionRec.h"
#include "HttpMime.h"
#include "Xml.h"
//#include "Links.h"
#include "HashTableT.h"
#include "Wiki.h"
#include "HttpServer.h"
#include "Speller.h"
#include "Repair.h"

static void scraperSleepWrapper      ( int fd , void *state ) ;
static void gotPhraseWrapper         ( void *state ) ;
//static void gotPagesWrapper          ( void *state , TcpSocket *s ) ;
//static void addedScrapedSitesWrapper ( void *state ) ;
//static void gotUrlInfoWrapper        ( void *state ) ;
//static void addedUrlsWrapper         ( void *state ) ;
static void indexedDocWrapper ( void *state );

Scraper g_scraper;

Scraper::Scraper ( ) {
	m_registered = false;
}

Scraper::~Scraper ( ) {
	// unregister it
	//if ( m_registered )
	//	g_loop.unregisterSleepCallback (NULL,scraperSleepWrapper);
}	

// returns false and sets g_errno on error
bool Scraper::init ( ) {
	// . set the sleep callback for once per 10 minutes
	// . this is in milliseconds. 1000 ms = 1 second
	int32_t wait = 1000; // 1000*60*10;
	if ( ! g_loop.registerSleepCallback(wait,NULL,scraperSleepWrapper))
		return false;
	m_registered = true;
	m_numReceived = 0;
	m_numSent     = 0;
	//m_bufEnd = m_buf + 50000;
	return true;
}

void scraperSleepWrapper ( int fd , void *state ) {
	g_scraper.wakeUp ( );
}

void Scraper::wakeUp ( ) {
	// this is wierd
	if ( m_numReceived != 0 || m_numSent != 0 ) {
		log("scraper: seems like a scrape is outstanding.");
		return;
	}
	// only host #0 scrapes
	if ( g_hostdb.m_myHost->m_hostId != 0 ) return;
	// no writing/adding if we are the tmp cluster
	if ( g_hostdb.m_useTmpCluster ) return;
	// scraping is off when repairing obviously
	if ( g_repairMode ) return;
	// . we are only allowed one collection scraping at a time
	// . find the collection, if any
	CollectionRec *cr = NULL;
	for ( int32_t i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) {
		cr = g_collectiondb.m_recs[i];
		if ( cr ) break;
	}
	// it was deleted, just return
	if ( ! cr ) return;

	// bail if scraping not enabled
	if ( ! cr->m_scrapingEnabledWeb   &&
	     ! cr->m_scrapingEnabledNews  &&
	     ! cr->m_scrapingEnabledBlogs &&
	     ! cr->m_scrapingEnabledProCog )
		return;

	// unregister it
	g_loop.unregisterSleepCallback (NULL,scraperSleepWrapper);
	m_registered = false;

	// get its coll
	strcpy ( m_coll , cr->m_coll );

	// try procog query log scraping
	//if ( cr->m_scrapingEnabledProCog )
	//	scrapeProCog();

	// bail now if only scraping procog
	if ( ! cr->m_scrapingEnabledWeb   &&
	     ! cr->m_scrapingEnabledNews  &&
	     ! cr->m_scrapingEnabledBlogs )
		return;

	log(LOG_INFO,"scraper: getting random phrase to scrape.");

	// get a random phrase from the wikipedia titles
	if ( ! g_wiki.getRandomPhrase ( NULL , gotPhraseWrapper ) ) return;
	// call it ourselves
	gotPhrase();
}

void gotPhraseWrapper ( void *state ) {
	g_scraper.gotPhrase();
}

void Scraper::gotPhrase ( ) {
	// error getting random phrase? bail!
	if ( g_errno ) log("scraper: got error getting random phrase: %s",
			   mstrerror(g_errno));

	CollectionRec *cr = g_collectiondb.getRec ( m_coll );

 loop:
	// what type of query should we do?
	m_qtype = rand() % 3;

	// make sure web, news, blog is enabled
	if ( m_qtype == 0 && ! cr->m_scrapingEnabledWeb   ) goto loop;
	if ( m_qtype == 1 && ! cr->m_scrapingEnabledNews  ) goto loop;
	if ( m_qtype == 2 && ! cr->m_scrapingEnabledBlogs ) goto loop;

	// scraping is off when repairing obviously
	if ( g_repairMode ) return;

	// get it
	char *s = g_wiki.m_randPhrase;
	// convert _'s to spaces
	for ( char *p = s ; *p ; p++ )
		if ( *p == '_' ) *p = ' ';
	// . url encode the random phrase
	// . truncate it to 200 bytes to keep things sane
	// . Wiki::doneReadingWiki() keeps it below 128 i think anyway
	char qe[400];
	urlEncode(qe, 200, s , gbstrlen(s) );
	char *end = qe + 390;

	// half the time append a random word from dictionary so that we 
	// discovery those tail-end sites better
	if ( m_qtype == 0 && (rand() % 2) ) { 
		// point into it for appending
		char *p = qe + gbstrlen(qe);
		// add a space, url encoded
		*p++ = '+';
		// append a random word to it from dictionary
		char *rw = g_speller.getRandomWord();
		// append that in
		urlEncode( p , end - p - 1 , rw , gbstrlen(rw) );
	}

	// make a query to scrape
	char buf[2048];

	char *uf ;
	if      ( m_qtype == 0 )
		uf="http://www.google.com/search?num=50&q=%s&scoring=d"
			"&filter=0";
	// google news query? sort by date.
	else if ( m_qtype == 1 )
		uf="http://news.google.com/news?num=50&q=%s&sort=n"
			"&filter=0";
	// google blog query?
	else if ( m_qtype == 2 ) 
		uf="http://www.google.com/blogsearch?num=50&q=%s&scoring=d"
			"&filter=0";
	// sanity check
	else { char *xx=NULL;*xx=0; }

	// make the url we will download
	sprintf ( buf , uf , qe );

	SpiderRequest sreq;
	// set the SpiderRequest
	strcpy(sreq.m_url, uf);
	// . tell it to only add the hosts of each outlink for now!
	// . that will be passed on to when XmlDoc calls Links::set() i guess
	// . xd will not reschedule the scraped url into spiderdb either
	sreq.m_isScraping = 1;
	sreq.m_fakeFirstIp = 1;
	int32_t firstIp = hash32n(uf);
	if ( firstIp == 0 || firstIp == -1 ) firstIp = 1;
	sreq.m_firstIp = firstIp;
	// parent docid is 0
	sreq.setKey(firstIp,0LL,false);

	// forceDEl = false, niceness = 0
	m_xd.set4 ( &sreq , NULL , m_coll , NULL , 0 ); 

	//m_xd.m_isScraping = true;

	// download without throttling
	//m_xd.m_throttleDownload = false;

	// disregard this
	m_xd.m_useRobotsTxt = false;

	// call this when index completes
	m_xd.setCallback ( NULL , indexedDocWrapper );

	// assume it blocked
	m_numSent++;

	// scraper is special
	m_xd.m_usePosdb     = false;
	//m_xd.m_useDatedb    = false;
	m_xd.m_useClusterdb = false;
	m_xd.m_useLinkdb    = false;
	m_xd.m_useSpiderdb  = true; // only this one i guess
	m_xd.m_useTitledb   = false;
	m_xd.m_useTagdb     = false;
	m_xd.m_usePlacedb   = false;
	//m_xd.m_useTimedb    = false;
	//m_xd.m_useSectiondb = false;
	//m_xd.m_useRevdb     = false;

	// . return false if this blocks
	// . will add the spider recs to spiderdb of the outlinks
	// . will add "ingoogle", etc. tags for each outlink
	if ( ! m_xd.indexDoc ( ) ) return ;

	// we didn't block
	indexedDoc ( );
}

void indexedDocWrapper ( void *state ) {
	// it did not block
	g_scraper.indexedDoc ( );
}

bool Scraper::indexedDoc ( ) {
	// got one completed now
	m_numReceived++;
	// if not done, leave
	if ( m_numReceived < m_numSent ) return false;
	// ok, all done, reset
	m_numSent     = 0;
	m_numReceived = 0;
	return true;
}
/*
// . uses parameters assigned to local member vars above
// . returns false if blocked, true otherwise
// . sets g_errno on error
void gotDocIdsWrapper ( void *state ) {
	Msg40 *THIS = (Msg40 *) state;
	// this should not block, just cache the score/docid from the search
	THIS->gotDocIds();
	// keep scraping!
	g_scraper.scrapeProCog();
}

bool Scraper::scrapeProCog ( ) {

	// done?
	if ( m_nextQuery >= m_queryBufEnd ) return true;

	// get the next query to scrape
	m_currentQuery = m_nextQuery;

	// advance
	m_nextQuery += gbstrlen(qptr) + 1;

	// do a local msg39
	if ( ! m_msg39 ) {
		try { m_msg39 = new ( Msg39 ); }
		catch ( ... ) {
			g_errno = ENOMEM;
			return false;
		}
		mnew ( m_msg39 , sizeof(Msg39),"scrm39");
	}

	// need to make the request
	m_r.reset();
	m_r.ptr_coll                    = m_coll;
	m_r.size_coll                   = gbstrlen(m_coll)+1;
	m_r.m_maxAge                    = 0; // no caching!
	m_r.m_addToCache                = false;
	m_r.m_docsToGet                 = 2; // m_docsToGet;
	m_r.m_niceness                  = MAX_NICENESS;
	m_r.m_isDebug                   = false;
	m_r.m_getDocIdScoringInfo       = false;
	m_r.m_doSiteClustering          = true;
	m_r.m_useMinAlgo                = false;
	m_r.m_useNewAlgo                = true; // for speed
	m_r.m_doMaxScoreAlgo            = true; // filter #1 of the new algo
	m_r.m_fastIntersection          = -1;
	m_r.m_doIpClustering            = true;
	m_r.m_doDupContentRemoval       = true;
	m_r.m_restrictIndexdbForQuery   = false;
	m_r.m_queryExpansion            = true;
	m_r.m_boolFlag                  = 2;
	m_r.m_familyFilter              = false;
	// TODO: make language that of the query! not always english
	m_r.m_language                  = langEnglish;
	m_r.ptr_query                   = m_currentQuery;
	m_r.size_query                  = gbstrlen(m_currentQuery)+1;
	m_r.m_timeout                   = 9999; // wait a int32_t time, in secs

	// callback hack
	m_msg39->m_callback = gotDocIdsWrapper;
	m_msg39->m_state    = NULL;
	m_msg39->m_queryHash32 = hash32n ( m_currentQuery );

	// . get the docIds
	// . this sets m_msg3a.m_clusterLevels[] for us
	// . TODO: consider limiting to first 20% of docids, i.e. so we'd
	//   have to read 5x less from disk!!
	if ( ! m_msg39->getDocIds2 ( &m_r ) )
		return false;

	// call again w/o parameters now
	return gotDocIds ( );
}	

// . return false if blocked, true otherwise
// . sets g_errno on error
bool Scraper::gotDocIds ( ) {
	// cache the top result
	//int64_t docId = m_msg39->m_topDocId;
	//float     score = m_msg39->m_topScore;
	// store in local cache
	int32_t key32 = m_msg39->m_queryHash32;
	// make the record
	char rec[128];
	char *p = rec;
	// the flag to indicate we're doing a single node query
	*p = QLRC_LOCALQUERY;
	p++;
	*(float *)p = m_msg39->m_topScore;
	p += sizeof(float);
	*(float *)p = m_msg39->m_topScore2;
	p += sizeof(float);
	// how big is that?
	int32_t recSize = p - rec;
	// cache it
	g_queryLogResultCache.addRec ( &key32 , rec , recSize );
}
*/
