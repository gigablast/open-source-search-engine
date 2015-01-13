#include "Msgaa.h"
#include "Tagdb.h"
#include "Msg40.h"

// test sites:
// blogs.ubc.ca/wetsocks/feed/
// my.donews.com/comboatme/2008/12/17/t-think-they-could-be-hydroxycut-hepatic-failure-upon/
// blogs.kaixo.com/maildepa/2008/12/23/by-thom-patterson-cnn-the-adorable-shemale-wave-of-millions-of-early/
// blog.seamolec.org/tim980/2008/12/30/purchase-thief-of-hearts-online/
// www.homeschoolblogger.com/sagerats/623595/
// blogs.x7web.com/lamonblog2427/
// blogs.msdn.com/predos_spot/

//
// POSSIBLE IMPROVEMENTS
//
// check usernames to be compound that when split the right way contain
// a common name, like "dave", "pedro" or "williams". and possibly discard
// subsites whose path contains categories or other names in the dictionary.

//
// POTENTIAL ARTICLE CONTENT IDENTIFCATION ALGORITHM
//
// identify all tag hashes (includes hashes of all parents) on the page
// which preceed unique content, only found on that page. minimize the list
// of all such tag hashes, and store into tagdb for that SITE. also store
// for that domain and hostname if those tag recs have less than 10-20 such
// tag hashes already. (tag hash hashes all the alpha chars in the tag 
// attributes as well. see DateParse.cpp ALGORITHM description)
// PROBLEM: duplicated pages, or printer-friendly pages! if pages is very
// similar content (not tags) to another (test using vectors) then toss it!!
// keep a count for each tag as how many pages think it is a repeated vs. 
// unique content.

static void gotResultsAAWrapper ( Msg40 *msg40 , void *state ) ;

Msgaa::Msgaa ( ) {
	m_msg40 = NULL;
}

Msgaa::~Msgaa ( ) {
	if ( m_msg40 ) {
		// free him, we sent his reply
		mdelete ( m_msg40 , sizeof(Msg40),"msgaa");
		delete  ( m_msg40 );
	}		
	m_msg40 = NULL;
}

#define SAMPLESIZE 100

// . also sets m_sitePathDepth to what it should be
// . -1 indicates unknown (not enough data, etc.) or host/domain is the site
// . returns false if blocked, true otherwise
// . returns true and sets g_errno on error
bool Msgaa::addSitePathDepth ( TagRec *gr  , 
			       Url    *url ,
			       char *coll ,
			       void *state ,
			       void (* callback)(void *state) ) {

	// save it
	m_gr            = gr;
	m_url           = url;
	m_coll          = coll;
	m_state         = state;
	m_callback      = callback;

	// set this to unknown for now
	m_sitePathDepth    = -1;
	m_oldSitePathDepth = -1;

	// reset this just in case
	g_errno = 0;

	// skip this for now!
	return true;

	CollectionRec *cr = g_collectiondb.getRec ( coll );
	if ( ! cr ) return true;
	if ( ! cr->m_subsiteDetectionEnabled ) return true;

	// check the current tag for an age
	Tag *tag = gr->getTag("sitepathdepth");
	// if there and the age is young, skip it
	int32_t age = -1;
	int32_t now = getTimeGlobal();
	if ( tag ) age = now - tag->m_timestamp;
	// if there, at least get it
	if ( tag ) m_oldSitePathDepth = (int32_t)tag->m_data[0];
	// if older than 30 days, we need to redo it
	if ( age > 30*24*60*60 ) age = -1;
	// if age is valid, skip it
	if ( age >= 0 ) {
		// just use what we had, it is not expired
		m_sitePathDepth = (int32_t)tag->m_data[0];
		// all done, we did not block
		return true;
	}

	// right now we only run on host #0 so we do not flood the cluster
	// with queries...
	if ( g_hostdb.m_hostId != 0 ) return true;


HASH all these things together into 1 termlist!!!
	gbsitesample:<pathdepth> term. everything else is constant. use msg0 then, not msg40

rename msgaa to Site.

  and use Site::set(Url *u) to do all this logic.
  or Site::set(TitleRec *tr) to set it exactly from title rec

	// make a new Msg40 to get search results with
	try { m_msg40 = new (Msg40); }
	catch ( ... ) {
		g_errno = ENOMEM;
		log("msgaa: new(%i): %s", sizeof(Msg40), mstrerror(g_errno));
		return true;
	}
	mnew ( m_msg40 , sizeof(Msg40) , "Msgaa" );

	// initial path depth
	m_pathDepth = 1;

	// see how many urls are non-cgi with a pathdepth of 1
	char *p = m_qbuf;
	strcpy ( p , "site:" );
	p += 5;
	gbmemcpy ( p , m_url->getHost() , m_url->getHostLen() );
	p += m_url->getHostLen();
	// sort them by the random score term, gbrandscore (see XmlDoc.cpp)
	p += sprintf (p ,
		      " gbpathdepth:%"INT32""
		      " gbiscgi:0"
		      " gbhasfilename:0"
		      // www.xyz.com/viacom-extends-brad-greys-contract/ not!
		      " gbpermalink:0 "
		      "| gbrandscore:1",
		      m_pathDepth);

	// set our SearchInput
	m_si.reset();
	// a sample of 100 results should be good!
	m_si.m_docsWanted             = SAMPLESIZE;
	m_si.m_requireAllTerms        = true;
	m_si.m_firstResultNum         = 0;
	m_si.m_coll                   = m_coll;
	m_si.m_doDupContentRemoval    = false;
	m_si.m_doSiteClustering       = false;
	m_si.m_docsToScanForReranking = 0;
	// the query
	m_si.m_query    = m_qbuf;
	m_si.m_queryLen = gbstrlen(m_qbuf);
	// sanity check
	if ( m_si.m_queryLen + 1 > MAX_QBUF_SIZE ) {char*xx=NULL;*xx=0; }
	// do not generate titles or summaries to save time, we just need
	// the url to check for bushiness. Msg20 if "getSummary" is false
	// will already not set the title...
	m_si.m_numLinesInSummary = 0;

	// perform the Msg40 query
	if ( ! m_msg40->getResults ( &m_si ,
				     false , // forward?
				     this  , // state
				     gotResultsAAWrapper ) )
		// return false if that blocks
		return false;
	// did not block
	return gotResults();
}

void gotResultsAAWrapper ( Msg40 *msg40 , void *state ) {
	Msgaa *THIS = (Msgaa *)state;
	THIS->gotResults();
}

// . look at the urls we got in the results
// . they should be of the for abc.xyz.com/yyyyyy/ 
// . returns true and sets g_errno on error
bool Msgaa::gotResults ( ) {
	// loop over each one
	int32_t n = m_msg40->m_numMsg20s;
	// we need at least half of requested to make a good estimation
	if ( n <= SAMPLESIZE/2 ) return true;
	// make a hashtable
	HashTable ht;
	// get the url
	for ( int32_t i = 0 ; i < n ; i++ ) {
		// get the ith result
		Msg20Reply *r = m_msg40->m_msg20[i]->m_r;
		// get the url string
		char *us    = r->ptr_ubuf;
		int32_t  uslen = r->size_ubuf - 1;
		Url u; u.set ( us , uslen );
		// get path component # m_pathDepth
		int32_t clen;
		char *c = u.getPathComponent ( i - 1 , &clen );
		// must be there
		if ( ! c || clen <= 0 ) {
			log("msgaa: c is empty");
			continue;
		}
		// now hash it
		int32_t h = hash32 ( c , clen );
		// count in table
		int32_t slot = ht.getSlot ( h );
		// how many times has this occurred in a result's url?
		int32_t count = 0;
		// inc if there
		if ( slot >= 0 ) count = ht.getValueFromSlot ( slot );
		// inc it
		count++;
		// put it back
		if ( slot >= 0 ) ht.setValue ( slot , count );
		// otherwise, add it new
		else if ( ! ht.addKey ( h , count ) ) return true;
	}
	// now scan the hash table and see how many unique path components
	int32_t unique = 0;
	int32_t ns = ht.getNumSlots();
	for ( int32_t i = 0 ;i < ns ; i++ ) {
		// is empty
		int32_t val = ht.getValueFromSlot ( i );
		// count if non-empty
		if ( val > 0 ) unique++;
	}
	// i'd say 50% of the SAMPLESIZE+ search results are required to have 
	// unique path components in order for us to consider this to be a 
	// subsite with a path depth of m_pathDepth.
	int32_t required = n / 2;
	if ( unique < required ) {
		// ok, do not set m_sitePathDepth, leave it -1
		log("msgaa: only have %"INT32" unique path components at path "
		    "depth %"INT32" out of %"INT32" results. %s does not have subsites.",
		    unique,m_pathDepth,n,m_url->getUrl());
		return true;
	}
	// i guess we got it
	log("msgaa: have %"INT32" unique path components at path "
	    "depth %"INT32" out of %"INT32" results. Enough to declare this as a "
	    "subsite for %s .",unique,m_pathDepth,n,m_url->getUrl());
	// ok set it
	m_sitePathDepth = m_pathDepth;
	// we are done
	return true;
}
