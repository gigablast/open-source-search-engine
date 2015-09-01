#include "SiteGetter.h"
#include "Url.h"
#include "Msg1.h"
#include "Rdb.h"
#include "Posdb.h"

//
// BASIC IDEA
//

// if we got xyz.com/a/b/c/index.html then we hash the root like:
// siteterm:xyz.com/a/b/c/ but we only hash that if we are an index.html or
// other identifiable index page. i.e. we need to look like we are the root
// of a subsite. we could also check the link structure? how?

// then if we got a ton of siteterm:xyz.com/a/b/*/ terms we figure that
// they must all be subsites.

// EXCEPTION: things like wikipedia? wikipedia doesn't seem to pose a prob itself

// examples:
// xyz.com/home/users/fred/
// xyz.com/home/users/jamie/
// xyz.com/home/users/bob/

// we only index the siteterm:* thing for certain urls. that is a key component
// of this algorithm. see XmlDoc::hashNoSplit() for those conditions!!!

// basically, if you are a root directory, you "vote" for your PARENT as the
// subsite by indexing your parent subdir with the siteterm: term prefix.

// so for the 3 examples above we would index:
// siteterm:xyz.com/home/users/
// siteterm:xyz.com/home/users/
// siteterm:xyz.com/home/users/

// thereby giving xyz.com/home/users/ a pretty good subsite score! but below
// you'll see that we need 100 such votes to be a subsite!




// this screws us up:
// http://svn.wp-plugins.org/
// BUT none of those "sub-site" pages have pure text content. maybe we can
// include that as a factor? and make sure the text is unique???
// furthermore we should only divide a site into subsites if its siteNumInlinks
// is high!!


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

static void gotSiteListWrapper ( void *state ) ;
//static void addedTagWrapper    ( void *state ) ;

SiteGetter::SiteGetter ( ) {
	m_siteLen = 0;
	m_site[0] = '\0';
	m_errno = 0;
}

SiteGetter::~SiteGetter ( ) {
}

// . also sets m_sitePathDepth to what it should be
// . -1 indicates unknown (not enough data, etc.) or host/domain is the site
// . returns false if blocked, true otherwise
// . returns true and sets g_errno on error
// . sets m_site to reference into "url" so XmlDoc::updateTagdb() can just
//   pass a bunch of site ptrs to msg9a
// . "url" MUST BE NORMALIZED via Url.cpp. so using Links' buffer is ok!
// . TODO: consider setting "state" to null if your url host has tons of inlinx
bool SiteGetter::getSite ( char   *url      ,
			   TagRec *gr       ,
			   int32_t    timestamp,
			   //char   *coll     ,
			   collnum_t collnum,
			   int32_t    niceness ,
			   //bool    addTags  ,
			   void   *state    ,
			   void (* callback)(void *state) ) {
	
	// save it
	m_gr       = gr;
	m_url      = url;
	//m_coll     = coll;
	m_collnum = collnum;
	//m_addTags  = addTags;
	m_state    = state;
	m_callback = callback;
	m_timestamp= timestamp;
	m_niceness = niceness;
	m_errno    = 0;

	// is it domain only?
	m_hasSubdomain = ::hasSubdomain ( url );

	// reset
	m_siteLen = 0;
	m_site[0] = '\0';
	m_allDone  = false;
	m_addedTag.reset();

	// set this to unknown for now
	m_sitePathDepth    = -1;
	m_oldSitePathDepth = -1;

	// reset this just in case
	g_errno = 0;

	//
	// HARDCODED algos
	//
	// ~ /user/ /users/ /profile/ myspace facebook linkedin
	//
	if ( setRecognizedSite ( ) ) {
		m_allDone = true;
		return true;
	}

	// bail if nothing else we can do
	if ( ! gr ) return setSite ( ) ;

	CollectionRec *cr = g_collectiondb.getRec ( collnum );
	// g_errno should be set if this is NULL
	if ( ! cr ) return true;
	//if ( ! cr->m_subsiteDetectionEnabled ) return true;

	// check the current tag for an age
	Tag *tag = gr->getTag("sitepathdepth");
	// if there and the age is young, skip it
	int32_t age = -1;
	//int32_t now = getTimeGlobal();
	//if ( tag ) age = now - tag->m_timestamp;
	// to parse conssitently for the qa test "qatest123" coll use 
	// "timestamp" as the "current time"
	if ( tag ) age = timestamp - tag->m_timestamp;
	// if there, at least get it (might be -1)
	if ( tag ) m_oldSitePathDepth = atol ( tag->getTagData() );
	// . if older than 10 days, we need to redo it
	// . if caller give us a timestamp of 0, never redo it!
	if ( age > 10*24*60*60 && timestamp != 0 ) age = -1;

	//if ( strstr(m_url,"http://www.topix.com/yp/albuquerque/c/community-religion-and-spirituality-churches") )
	//	log("hey");

	// . if our site quality is low, forget about dividing it up too
	// . if age is valid, skip it
	// . also if caller does not want a callback, like XmlDoc.cpp,
	//   then use whatever we got
	if ( age >= 0 || ! m_state ) { // || hostRootNumInlinks < 500 ) {
		// do not add to tagdb
		m_state = NULL;
		// just use what we had, it is not expired
		m_sitePathDepth = m_oldSitePathDepth;
		// . now set the site with m_sitePathDepth
		// . sanity check, should not block since m_state is NULL
		if ( ! setSite () ) { char *xx=NULL;*xx=0; }
		// we did not block
		return true;
	}

	// right now we only run on host #0 so we do not flood the cluster
	// with queries...
	if ( g_hostdb.m_hostId != 0 ) { 
		// do not add to tagdb and do not block!
		m_state = NULL;
		// . use a sitepathdepth of -1 by default then, until host #0
		//   has a chance to evaluate
		// . a sitepathdepth of -1 means to use the full hostname
		//   as the site
		m_sitePathDepth = -1;
		// sanity check, should not block since m_state is NULL
		if ( ! setSite () ) { char *xx=NULL;*xx=0; }
		// we did not block
		return true;
	}

	// . initial path depth
	// . this actually includes the first subdir name, up to, but not
	//   including the /, according to Url::getPathEnd()
	// . start with the broadest site as our possible subsite first
	//   in order to reduce errors i guess. because if we have examples:
	//   xyz.com/fred/
	//   xyz.com/jamie/
	//   xyz.com/bob/ ...
	//   and we also have:
	//   xyz.com/home/users/fred/
	//   xyz.com/home/users/jamie/
	//   xyz.com/home/users/bob/ ...
	//   then we need the first set to take precedence!
	m_pathDepth = 0;

	// set our fill url class. do not addWWW
	//m_u.set ( m_url , gbstrlen(m_url) , false );

	// must have http:// i guess
	if ( strncmp(m_url,"http",4) ) { 
		g_errno = EBADURL;
		return true;
		// don't let bad input from pageparser core us!
		char *xx=NULL;*xx=0;
	}

	// how many can we do? false = countFilename?
	//m_maxPathDepth = m_u.getPathDepth ( false );

	// . pathDepth==0 for "www.xyz.com"
	// . pathDepth==0 for "www.xyz.com/"
	// . pathDepth==0 for "www.xyz.com/foo"
	// . pathDepth==1 for "www.xyz.com/foo/"
	// . pathDepth==1 for "www.xyz.com/foo/x"
	// . pathDepth==2 for "www.xyz.com/foo/x/"
	// . pathDepth==2 for "www.xyz.com/foo/x/y"
	// . true --> we have the protocol, http:// in m_url
	m_maxPathDepth = getPathDepth ( m_url , true );

	// get it. return false if it blocked.
	return getSiteList();
}


// . returns false if blocked, true otherwise
// . returns true on error and sets g_errno
bool SiteGetter::getSiteList ( ) {

top:
	// . setSite() will return TRUE and set g_errno on error, and returns
	//   false if it blocked adding a tag, which will call callback once
	//   tag is added
	// . stop at this point
	if ( m_pathDepth >= 3 ) return setSite();
	// or if no more
	if ( m_pathDepth >= m_maxPathDepth ) return setSite();

	// . make the termid
	// . but here we get are based on "m_pathDepth" which ranges
	//   from 1 to N
	// . if m_pathDepth==0 use "www.xyz.com" as site
	// . if m_pathDepth==1 use "www.xyz.com/foo/" as site ...
	char *pend = getPathEnd ( m_url , m_pathDepth );
	// hash up to that
	//char *host = m_u.getHost();
	char *host = getHostFast ( m_url , NULL );
	// hash the prefix first to match XmlDoc::hashNoSplit()
	char *prefix = "siteterm";
	// hash that and we will incorporate it to match XmlDoc::hashNoSplit()
	int64_t ph = hash64 ( prefix , gbstrlen(prefix) );
	// . this should match basically what is in XmlDoc.cpp::hash()
	// . and this now does not include pages that have no outlinks 
	//   "underneath" them.
	int64_t termId = hash64 ( host , pend - host , ph ) & TERMID_MASK;

	// get all pages that have this as their termid!
	key144_t start ;
	key144_t end   ;
	g_posdb.makeStartKey ( &start, termId );
	g_posdb.makeEndKey   ( &end  , termId );

	// . now see how many urls art at this path depth from this hostname
	// . if it is a huge # then we know they are all subsites!
	//   because it is too bushy to be anything else
	// . i'd say 100 nodes is good enough to qualify as a homestead site

	int32_t minRecSizes = 5000000;
	// get the group this list is in
	//uint32_t gid ;
	//gid = getGroupId ( RDB_POSDB , (char *)&start , false ); //split?
	//uint32_t shardNum ;
	//shardNum = getShardNum( RDB_POSDB , (char *)&start , false ); //split?

	// i guess this is split by termid and not docid????
	int32_t shardNum = g_hostdb.getShardNumByTermId ( &start );

	// we need a group #. the column #.
	//int32_t split = g_hostdb.getGroupNum ( gid );
	// int16_tcut
	Msg0 *m = &m_msg0;
	// get the list. returns false if blocked.
	if ( ! m->getList ( -1                 , // hostId
			    0                  , // ip
			    0                  , // port
			    0                  , // maxCacheAge
			    false              , // addToCache
			    RDB_POSDB        ,
			    m_collnum             ,
			    &m_list            ,
			    (char *)&start     ,
			    (char *)&end       ,
			    minRecSizes        ,
			    this               ,
			    gotSiteListWrapper ,
			    m_niceness         , // MAX_NICENESS
			    // default parms follow
			    true  ,  // doErrorCorrection?
			    true  ,  // includeTree?
			    true  ,  // doMerge?
			    -1    ,  // firstHostId
			    0     ,  // startFileNum
			    -1    ,  // numFiles
			    999999,  // timeout
			    -1    ,  // syncPoint
			    -1    ,  // preferLocalReads
			    NULL  ,  // msg5
			    NULL  ,  // msg5b
			    false ,  // isrealmerge?
			    true  ,  // allowpagecache?
			    false ,  // forceLocalIndexdb?
			    false ,  // doIndexdbSplit? nosplit
			    shardNum ) )//split ))
		return false;

	// return false if this blocked
	if ( ! gotSiteList() ) return false;
	// error?
	if ( g_errno ) return true;
	// or all done
	if ( m_allDone ) return true;
	// otherwise, try the next path component!
	goto top;
}

void gotSiteListWrapper ( void *state ) {
	SiteGetter *THIS = (SiteGetter *)state;
	if ( ! THIS->gotSiteList() ) return;
	// try again?
	if ( THIS->m_tryAgain ) {
		// return if blocked
		if ( ! THIS->getSiteList() ) return;
		// otherwise, if did not block, we are really done because
		// it loops until it blocks
	}
	// call callback if all done now
	THIS->m_callback ( THIS->m_state );
}

// . returns false if blocked, returns true and sets g_errno on error
// . returns true with m_allDone set to false to process another subsite
// . we use voters to set SEC_VOTE_STATIC and SEC_VOTE_DYNAMIC flags
//   in addition to SEC_VOTE_TEXTY and SEC_VOTE_UNIQUE
bool SiteGetter::gotSiteList ( ) {
	// assume not trying again
	m_tryAgain = false;
	// error?
	if ( g_errno ) {
		// timeouts usually...
		log("site: sitegetter gotList: %s",mstrerror(g_errno));
		// mark it so caller knows
		m_errno = g_errno;
		// so try again without increasing m_pathDepth
		// i've seen a host return EBADRDBID for some reason
		// and put host #0 in an infinite log spam loop so stop it
		if ( g_errno != EBADRDBID ) m_tryAgain = true;
		return true;
	}
	// how many urls at this path depth?
	int32_t count = ( m_list.getListSize() - 6 ) / 6;
	// if we do not have enough to quality this as a subsite path depth
	// try the next
	if ( count < 100 ) { 
		// increment and try again
		m_pathDepth++; 
		// clear just in case
		g_errno = 0;
		// get another list if we can, m_allDone is no true yet
		if ( m_pathDepth < m_maxPathDepth ) {
			m_tryAgain = true;
			return true;
		}
	}

	// ok, i guess this indicates we have a subsite level
	m_sitePathDepth = m_pathDepth;

	// this basically means none!
	if ( m_pathDepth >= m_maxPathDepth ) m_sitePathDepth = -1;

	// . sets m_site and m_siteLen from m_url
	// . this returns false if blocked, true otherwise
	return setSite ( ) ;
}

//void addedTagWrapper ( void *state ) {
//	SiteGetter *THIS = (SiteGetter *)state;
//	// all done
//	THIS->m_callback ( THIS->m_state );
//}

// . return false if blocked, return true with g_errno set on error
// . returns true if did not block
bool SiteGetter::setSite ( ) {

	// no more looping
	m_allDone = true;

	// assume this is false for now
	//m_isIndependentSubsite = false;


	// we need to distinguish this from the "site" tags that
	// Tagdb.cpp adds to new TagRecs that do not have a "site" tag. but
	// for now maybe we can comment out?

	/*
	  MDW: take out for now
	// . extract from tag rec
	// . must not have a "sitepathdepth" for us to do this
	Tag *tag = NULL;
	// get the "site" tag if we should
	if ( m_gr && m_sitePathDepth == -1 ) tag = m_gr->getTag("site");
	// . if we hade a "site" tag and no "sitepathdepth" tag, use that site
	// . but if sitepathdepth is zero, ignore this too and use the hostname
	//   as the site. i never want to use domains themselves as a site
	//   because XmlDoc::getSite() ends up returning a domain only
	//   and thus XmlDoc::getRootXmlDoc() uses the domain as the 
	//   url and that is not what we want for XmlDoc::getRootLangId()
	if ( tag && tag->getTagDataSize() > 1 && m_sitePathDepth > 0 ) {
		// get length
		m_siteLen = tag->getTagDataSize() - 1;
		// sanity check
		if ( m_siteLen > MAX_SITE_LEN ) {
			m_site [ 0 ] = '\0';
			m_siteLen = 0;
			g_errno = EURLTOOBIG;
			return true;
		}
		// get the data, including terminating \0
		gbmemcpy ( m_site , tag->getTagData() , m_siteLen + 1 );
		// sanity check - must include the \0
		if (m_site[m_siteLen]!= '\0') {char*xx=NULL;*xx=0;}
		// all done
		return true;
	}
	*/

	// . get the host of our normalized url
	// . assume the hostname is the site
	int32_t hostLen;
	char *host = ::getHost ( m_url , &hostLen );

	// no, assume domain since Tagdb.cpp adds the domain as the value
	// for the "site" tag when it adds a TagRec to Tagdb
	//char *site = ::getDomFast ( m_url , &m_siteLen );

	// truncated?
	if ( hostLen + 6 > MAX_SITE_LEN ) {
		m_site [ 0 ] = '\0';
		m_siteLen = 0;
		g_errno = EURLTOOBIG;
		return true;
	}

	char *x = m_site;
	// check it
	if ( ! m_hasSubdomain ) {
		gbmemcpy ( x , "www.", 4 );
		x += 4;
	}
	// save it
	gbmemcpy ( x , host , hostLen );
	x += hostLen;

	m_siteLen = x - m_site;

	// null terminate
	m_site [ m_siteLen ] = '\0';

	// . -1 means to use the hostname as the site
	// . i am trying to obsolete the site filters table here...
	//if ( m_sitePathDepth == -1 ) return true;

	// TODO:
	// if we are linked to by the hostname root, ignore sitePathDepth
	// as well. that way we separate the subsites from subdirs off the root

	// we are an independent subsite. Tagdb.cpp needs to know this so
	// it will not inherit from the root url's tagdb rec
	//m_isIndependentSubsite = true;

	// . otherwise we got a subsite
	// . if m_pathDepth==0 use "www.xyz.com" as site
	// . if m_pathDepth==1 use "www.xyz.com/foo/" as site ...
	/*
	  MDW: take out for now
	if ( m_sitePathDepth >= 0 ) {
		char *end = getPathEnd ( m_url , m_sitePathDepth );
		// set the site length
		m_siteLen = end - site;
	}

	// truncated?
	if ( m_siteLen > MAX_SITE_LEN ) {
		m_site [ 0 ] = '\0';
		m_siteLen = 0;
		g_errno = EURLTOOBIG;
		return true;
	}
	*/


	//logf(LOG_DEBUG,"site: site of %s is %s",m_url,m_site);

	// do not add if this is NULL, caller does not want us to block
	//if ( ! m_state ) return true;

	//if ( ! m_addTags ) return true;

	return true;
	/*
	// . to prevent slamming indexdb, store the site in tagdb now
	//   make it a string so tagdb likes it
	// . this could be -1 which indicates to use hostname!
	char buf[12];
	sprintf ( buf , "%"INT32"",m_sitePathDepth);

	// sanity check
	if ( m_timestamp == 0 ) { char *xx=NULL;*xx=0; }

	// . now update tagdb with the new date and path depth
	// . this might be -1, which is nice because it gives us a timestamp
	//   so we only have to do this like once every 2 weeks or so
	//TagRec gr;
	m_addedTag.addTag ( "sitepathdepth" , 
			    // now XmlDoc must provide it to ensure that are 
			    // injects into the "qatest123" coll are consistent
			    m_timestamp     ,//getTime()// use now as timestamp
			    "sitegit"       , // username
			    0               , // ip
			    buf             , // data
			    gbstrlen(buf)+1   );// dateSize (includes \0)

	// we apply the sitepathdepth tag to tag for this subdomain
	int32_t hlen; char *host = getHostFast ( m_url , &hlen );

	// null term temporarily
	char c = host[hlen];
	host[hlen] = '\0';

	// add it
	bool status = m_msg9a.addTags ( host            , // "sites"
					NULL            , // site ptrs
					0               , // num site ptrs
					m_coll          ,
					this            ,
					addedTagWrapper ,
					m_niceness      ,
					// contains the tags to add
					&m_addedTag     ,
					false           , // nuke tags?
					NULL            );// ip vec
	// remove NULL
	host[hlen] = c;

	// return false if blocked, true otherwise (g_errno could be set)
	return status;
	*/
}

//
// hardcoded support for popular formats and sites
//
bool SiteGetter::setRecognizedSite ( ) {

	// clear just in case
	g_errno = 0;

	// get path of url
	char *p = m_url;
	for ( ; *p && *p != ':' ; p++ );
	// error?
	if ( *p != ':' ) return false;
	// skip ://
	p += 3;
	// save host ptr
	char *host = p;
	// then another / for the path
	for ( ; *p && *p != '/' ; p++ );
	// error?
	if ( *p != '/' ) return false;
	//
	// ok, "p" now points to the path
	//
	char *path = p;

	// convenience vars
	int32_t  len = 0;

	// . deal with site indicators
	// . these are applied to all domains uniformly
	// . if it is xyz.com/users/  use xyz.com/users/fred/ as the site

	// a lot of times these were not indivual blogs, but the blog subsite
	// of a site... http://dccc.org/blog/P4575/
	//if ( strncasecmp(p,"/blogs/"       , 7) == 0 ) len = 7;
	//if ( strncasecmp(p,"/blog/"        , 6) == 0 ) len = 6;
	// commented out a bunch cuz they were profiles mostly, not blogs...
	if ( strncasecmp(p,"/~"            , 2) == 0 ) len = 2;
	// assume this is a username. skip the first /
	//if ( sitepathdepth == 1                      ) len = 1;
	if ( strncasecmp(p,"/users/"       , 7) == 0 ) len = 7;
	if ( strncasecmp(p,"/user/"        , 6) == 0 ) len = 6;
	if ( strncasecmp(p,"/members/"     , 9) == 0 ) len = 9;
	if ( strncasecmp(p,"/membres/"     , 9) == 0 ) len = 9;
	if ( strncasecmp(p,"/member/"      , 8) == 0 ) len = 8;
	if ( strncasecmp(p,"/membre/"      , 8) == 0 ) len = 8;
	if ( strncasecmp(p,"/member.php?u=",14) == 0 ) len = 14;

	// point to after the /users/, /blogs/, /user/, /blog/ or /~xxx/
	p += len;
	// assume there is NOT an alpha char after this
	char username = false;
	// . skip to next / OR ?
	// . stop at . or -, because we do not allow those in usernames and
	//   they are often indicative of filenames without file extensions
	// . no, fix http://www.rus-obr.ru/users/maksim-sokolov (no - or _ or.)
	while ( len && *p && *p!= '/'&&*p!='?' ) {
		// sometimes usernames are numbers!!!
		//if ( is_alpha_a(*p) ) username = true;
		// http://stackoverflow.com/users/271376/sigterm
		if ( is_alnum_a(*p) ) username = true;
		p++;
	}
	// if we hit this, not a username
	//if ( *p=='.' || *p == '-' || *p == '_' ) username = false;
	// did we get a match?
	// . www.cits.ucsb.edu/users/michael-osborne
	// . www.cits.ucsb.edu/users/michael-osborne/
	// . after /blog/ or /~ should be another / or \0, not a period,
	//   because that indicates probably a filename, which is not right,
	//   because we are expecting a username!
	if ( username && p - host + 6 < MAX_SITE_LEN ) {
		// jump up here to store
	storeIt:
		// for parsing
		char *x = m_site;
		// store www first if its a domain only url
		if ( ! m_hasSubdomain ) {
			gbmemcpy ( x , "www." , 4 );
			x += 4;
		}
		// store it
		gbmemcpy ( x , host , p - host );
		x += p - host;
		// set the length of it
		m_siteLen = x - m_site;
		// make it end on a '/' if we can
		if ( m_site[m_siteLen-1] != '/' &&
		     // watch out for /?uid=xxxx crap
		     m_site[m_siteLen-1] != '=' ) {
			// force the / then
			m_site[m_siteLen] = '/';
			m_siteLen++;
		}
		// null term the site
		m_site [ m_siteLen ] = '\0';
		return true;
	}


	//
	// popular homesteads
	//
	int32_t depth = 0;
	// term host
	char c = *path;
	*path = '\0';
	if ( strstr(host,"vimeo.com"      ) ) depth = 1;
	if ( strstr(host,"www.myspace.com") ) depth = 1;
	if ( strstr(host,"twitter.com"    ) ) depth = 1;
	if ( strstr(host,"www.facebook.com") ) depth = 1;
	// revert
	*path = c;

	// return false to indicate no recognized site detected
	if ( ! depth ) return false;

	// skip over the initial root / after the hostname
	p = path + 1;

	// no path really? root path? just return the hostname then
	if ( ! *p  && path - host + 6 < MAX_SITE_LEN ) {
		// for parsing
		char *x = m_site;
		// store www first if its a domain only url
		if ( ! m_hasSubdomain ) {
			gbmemcpy ( x , "www." , 4 );
			x += 4;
		}
		// store it
		gbmemcpy ( x , host , path - host );
		x += path - host;
		m_siteLen = x - m_site;
		m_site [ m_siteLen ] = '\0';
		return true;
	}

	// for depth
	for ( ; *p ; p++ ) 
		if ( *p == '/' && --depth == 0 ) break;

	if ( p - host + 6 >= MAX_SITE_LEN ) return false;

	goto storeIt;

	return true;
}






















