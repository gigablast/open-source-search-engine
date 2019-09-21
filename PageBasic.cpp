#include "SafeBuf.h"
#include "HttpRequest.h"
#include "SearchInput.h"
#include "Pages.h"
#include "Parms.h"
#include "Spider.h"
#include "PageResults.h" // for RESULT_HEIGHT
#include "Stats.h"

bool printFrontPageShell ( SafeBuf *sb , char *tabName , CollectionRec *cr ,
			   bool printGigablast ) ;

// 5 seconds
#define DEFAULT_WIDGET_RELOAD 1000

//bool printSitePatternExamples ( SafeBuf *sb , HttpRequest *hr ) ;

///////////
//
// main > Basic > Settings
//
///////////
/*
bool sendPageBasicSettings ( TcpSocket *socket , HttpRequest *hr ) {

	char  buf [ 128000 ];
	SafeBuf sb(buf,128000);

	// true = usedefault coll?
	CollectionRec *cr = g_collectiondb.getRec ( hr , true );
	if ( ! cr ) {
		g_httpServer.sendErrorReply(socket,500,"invalid collection");
		return true;
	}

	// process any incoming request
	handleSettingsRequest ( socket , hr );

	// . print standard header 
	// . this prints the <form tag as well
	g_pages.printAdminTop ( &sb , socket , hr );


	g_parms.printParms ( &sb , socket , hr );


	printSitePatternExamples ( &sb , hr );

	// wrap up the form, print a submit button
	g_pages.printAdminBottom ( &sb );


	return g_httpServer.sendDynamicPage ( socket,
					      sb.getBufStart() ,
					      sb.length()      , 
					      -1               ,
					      false,//POSTReply        ,
					      NULL             , // contType
					      -1               , // httpstatus
					      NULL,//cookie           ,
					      NULL             );// charset
}
*/

class PatternData {
public:
	// hash of the subdomain or domain for this line in sitelist
	int32_t m_thingHash32;
	// ptr to the line in CollectionRec::m_siteListBuf
	int32_t m_patternStrOff;
	// offset of the url path in the pattern, 0 means none
	int16_t m_pathOff; 
	int16_t m_pathLen;
	// offset into buffer. for 'tag:shallow site:walmart.com' type stuff
	int32_t  m_tagOff;
	int16_t m_tagLen;
};

void doneAddingSeedsWrapper ( void *state ) {
	// note it
	log("basic: done adding seeds using msg4");
}

// . Collectiondb.cpp calls this when any parm flagged with 
//   PF_REBUILDURLFILTERS is updated
// . it only adds sites via msg4 that are in "siteListArg" but NOT in the
//   current CollectionRec::m_siteListBuf
// . updates SpiderColl::m_siteListDomTable to see what doms we can spider
// . updates SpiderColl::m_negSubstringBuf and m_posSubStringBuf to
//   see what substrings in urls are disallowed/allowable for spidering
// . this returns false if it blocks
// . returns true and sets g_errno on error
// . uses msg4 to add seeds to spiderdb if necessary if "siteListArg"
//   has new urls that are not currently in cr->m_siteListBuf
// . only adds seeds for the shard we are on iff we are responsible for
//   the fake firstip!!! that way only one shard does the add.
bool updateSiteListBuf ( collnum_t collnum , 
			    bool addSeeds ,
			    char *siteListArg ) {

	CollectionRec *cr = g_collectiondb.getRec ( collnum );
	if ( ! cr ) return true;

	// tell spiderloop to update the active list in case this
	// collection suddenly becomes active
	g_spiderLoop.m_activeListValid = false;

	// this might make a new spidercoll...
	SpiderColl *sc = g_spiderCache.getSpiderColl ( cr->m_collnum );

	// sanity. if in use we should not even be here
	if ( sc->m_msg4x.m_inUse ) { 
		log("basic: trying to update site list while previous "
		    "update still outstanding.");
		g_errno = EBADENGINEER;
		return true;
	}

	// when sitelist is update Parms.cpp should invalidate this flag!
	//if ( sc->m_siteListTableValid ) return true;

	// hash current sitelist entries, each line so we don't add
	// dup requests into spiderdb i guess...
	HashTableX dedup;
	if ( ! dedup.set ( 4,0,1024,NULL,0,false,0,"sldt") ) return true;
	// this is a safebuf PARM in Parms.cpp now HOWEVER, not really
	// because we set it here from a call to CommandUpdateSiteList()
	// because it requires all this computational crap.
	char *op = cr->m_siteListBuf.getBufStart();

	// scan and hash each line in it
	for ( ; ; ) {
		// done?
		if ( ! *op ) break;
		// skip spaces
		if ( is_wspace_a(*op) ) op++;
		// done?
		if ( ! *op ) break;
		// get end
		char *s = op;
		// skip to end of line marker
		for ( ; *op && *op != '\n' ; op++ ) ;
		// keep it simple
		int32_t h32 = hash32 ( s , op - s );
		// for deduping
		if ( ! dedup.addKey ( &h32 ) ) return true;
	}

	// get the old sitelist Domain Hash to PatternData mapping table
	// which tells us what domains, subdomains or paths we can or
	// can not spider...
	HashTableX *dt = &sc->m_siteListDomTable;

	// reset it
	if ( ! dt->set ( 4 , 
			 sizeof(PatternData),
			 1024 ,
			 NULL , 
			 0 ,
			 true , // allow dup keys?
			 0 , // niceness - at least for now
			 "sldt" ) )
		return true;


	// clear old shit
	sc->m_posSubstringBuf.purge();
	sc->m_negSubstringBuf.purge();

	// we can now free the old site list methinks
	//cr->m_siteListBuf.purge();

	// reset flags
	//sc->m_siteListAsteriskLine = NULL;
	sc->m_siteListHasNegatives = false;
	sc->m_siteListIsEmpty = true;
	
	sc->m_siteListIsEmptyValid = true;

	sc->m_siteListIsEmptyValid = true;

	// use this so it will be free automatically when msg4 completes!
	SafeBuf *spiderReqBuf = &sc->m_msg4x.m_tmpBuf;

	//char *siteList = cr->m_siteListBuf.getBufStart();

	// scan the list
	char *pn = siteListArg;

	// completely empty?
	if ( ! pn ) return true;

	int32_t lineNum = 1;

	int32_t added = 0;

	Url u;

	for ( ; *pn ; lineNum++ ) {

		// get end
		char *s = pn;
		// skip to end of line marker
		for ( ; *pn && *pn != '\n' ; pn++ ) ;

		// point to the pattern (skips over "tag:xxx " if there)
		char *patternStart = s;

		// back p up over spaces in case ended in spaces
	        char *pe = pn;
		for ( ; pe > s && is_wspace_a(pe[-1]) ; pe-- );

		// skip over the \n so pn points to next line for next time
		if ( *pn == '\n' ) pn++;

		// make hash of the line
		int32_t h32 = hash32 ( s , pe - s );

		bool seedMe = true;
		bool isUrl = true;
		bool isNeg = false;
		bool isFilter = true;

		// skip spaces at start of line
		for ( ; *s && *s == ' ' ; s++ );

		// comment?
		if ( *s == '#' ) continue;

		// empty line?
		if ( s[0] == '\r' && s[1] == '\n' ) { s++; continue; }

		// empty line?
		if ( *s == '\n' ) continue;

		// all?
		//if ( *s == '*' ) {
		//	sc->m_siteListAsteriskLine = start;
		//	continue;
		//}

		char *tag = NULL;
		int32_t tagLen = 0;

	innerLoop:

		// skip spaces
		for ( ; *s && *s == ' ' ; s++ );


		// exact:?
		//if ( strncmp(s,"exact:",6) == 0 ) {
		//	s += 6;
		//	goto innerLoop;
		//}

		// these will be manual adds and should pass url filters
		// because they have the "ismanual" directive override
		if ( strncmp(s,"seed:",5) == 0 ) {
			s += 5;
			isFilter = false;
			goto innerLoop;
		}


		// does it start with "tag:xxxxx "?
		if ( *s == 't' && 
		     s[1] == 'a' &&
		     s[2] == 'g' &&
		     s[3] == ':' ) {
			tag = s+4;
			for ( ; *s && ! is_wspace_a(*s) ; s++ );
			tagLen = s - tag;
			// skip over white space after tag:xxxx so "s"
			// point to the url or contains: or whatever
			for ( ; *s && is_wspace_a(*s) ; s++ );
			// set pattern start to AFTER the tag stuff
			patternStart = s;
		}

		if ( *s == '-' ) {
			sc->m_siteListHasNegatives = true;
			isNeg = true;
			s++;
		}

		if ( strncmp(s,"site:",5) == 0 ) {
			s += 5;
			seedMe = false;
			goto innerLoop;
		}

		if ( strncmp(s,"contains:",9) == 0 ) {
			s += 9;
			seedMe = false;
			isUrl = false;
			goto innerLoop;
		}

		int32_t slen = pe - s;

		// empty line?
		if ( slen <= 0 ) 
			continue;

		// add to string buffers
		if ( ! isUrl && isNeg ) {
			if ( !sc->m_negSubstringBuf.safeMemcpy(s,slen))
				return true;
			if ( !sc->m_negSubstringBuf.pushChar('\0') )
				return true;
			if ( ! tagLen ) continue;
			// append tag
			if ( !sc->m_negSubstringBuf.safeMemcpy("tag:",4))
				return true;
			if ( !sc->m_negSubstringBuf.safeMemcpy(tag,tagLen) ) 
				return true;
			if ( !sc->m_negSubstringBuf.pushChar('\0') )
				return true;
		}
		if ( ! isUrl ) {
			// add to string buffers
			if ( ! sc->m_posSubstringBuf.safeMemcpy(s,slen) )
				return true;
			if ( ! sc->m_posSubstringBuf.pushChar('\0') )
				return true;
			if ( ! tagLen ) continue;
			// append tag
			if ( !sc->m_posSubstringBuf.safeMemcpy("tag:",4))
				return true;
			if ( !sc->m_posSubstringBuf.safeMemcpy(tag,tagLen) ) 
				return true;
			if ( !sc->m_posSubstringBuf.pushChar('\0') )
				return true;
			continue;
		}


		u.set ( s , slen );

		// error? skip it then...
		if ( u.getHostLen() <= 0 ) {
			log("basic: error on line #%"INT32" in sitelist",lineNum);
			continue;
		}

		// is fake ip assigned to us?
		int32_t firstIp = getFakeIpForUrl2 ( &u );

		if ( ! isAssignedToUs( firstIp ) ) continue;

		// see if in existing table for existing site list
		if ( addSeeds &&
		     // a "site:" directive mean no seeding
		     // a "contains:" directive mean no seeding
		     seedMe &&
		     // do not seed stuff after tag:xxx directives
		     // no, we need to seed it to avoid confusion. if
		     // they don't want it seeded they can use site: after
		     // the tag:
		     //! tag &&
		     ! dedup.isInTable ( &h32 ) ) {
			// make spider request
			SpiderRequest sreq;
			sreq.setFromAddUrl ( u.getUrl() );
			if ( 
			    // . add this url to spiderdb as a spiderrequest
			     // . calling msg4 will be the last thing we do
			    !spiderReqBuf->safeMemcpy(&sreq,sreq.getRecSize()))
				return true;
			// count it
			added++;

		}

		// if it is a "seed: xyz.com" thing it is seed only
		// do not use it for a filter rule
		if ( ! isFilter ) continue;
		
		
		// make the data node used for filtering urls during spidering
		PatternData pd;
		// hash of the subdomain or domain for this line in sitelist
		pd.m_thingHash32 = u.getHostHash32();
		// . ptr to the line in CollectionRec::m_siteListBuf. 
		// . includes pointing to "exact:" too i guess and tag: later.
		// . store offset since CommandUpdateSiteList() passes us
		//   a temp buf that will be freed before copying the buf
		//   over to its permanent place at cr->m_siteListBuf
		pd.m_patternStrOff = patternStart - siteListArg;
		// offset of the url path in the pattern, 0 means none
		pd.m_pathOff = 0;
		// did we have a tag?
		if ( tag ) {
			pd.m_tagOff = tag - siteListArg;
			pd.m_tagLen = tagLen;
		}
		else {
			pd.m_tagOff = -1;
			pd.m_tagLen = 0;
		}
		// scan url pattern, it should start at "s"
		char *x = s;
		// go all the way to the end
		for ( ; *x && x < pe ; x++ ) {
			// skip ://
			if ( x[0] == ':' && x[1] =='/' && x[2] == '/' ) {
				x += 2;
				continue;
			}
			// stop if we hit another /, that is path start
			if ( x[0] != '/' ) continue;
			x++;
			// empty path besides the /?
			if (  x >= pe   ) break;
			// ok, we got something here i think
			// no, might be like http://xyz.com/?poo
			//if ( u.getPathLen() <= 1 ) { char *xx=NULL;*xx=0; }
			// calc length from "start" of line so we can
			// jump to the path quickly for compares. inc "/"
			pd.m_pathOff = (x-1) - patternStart;
			pd.m_pathLen = pe - (x-1);
			break;
		}

		// add to new dt
		int32_t domHash32 = u.getDomainHash32();
		if ( ! dt->addKey ( &domHash32 , &pd ) )
			return true;

		// we have some patterns in there
		sc->m_siteListIsEmpty = false;
	}

	// go back to a high niceness
	dt->m_niceness = MAX_NICENESS;

	//int32_t siteListLen = gbstrlen(siteList);
	//cr->m_siteListBuf.safeMemcpy ( siteList , siteListLen + 1 );

	if ( ! addSeeds ) return true;

	log("spider: adding %"INT32" seed urls",added);

	// use spidercoll to contain this msg4 but if in use it
	// won't be able to be deleted until it comes back..
	if ( ! sc->m_msg4x.addMetaList ( spiderReqBuf ,
					 sc->m_collnum ,
					 // no need for callback since m_msg4x
					 // should set msg4::m_inUse to false
					 // when it comes back.
					 // crap if we don't have a callback
					 // Msg4.cpp::storeLineWaiters()
					 // will core
					 sc , // state
					 doneAddingSeedsWrapper, // callback 
					 MAX_NICENESS ,
					 RDB_SPIDERDB
					 ) )
		return false;

	return true;
}

// . Spider.cpp calls this to see if a url it wants to spider is
//   in our "site list"
// . we should return the row of the FIRST match really
// . the url patterns all contain a domain now, so this can use the domain
//   hash to speed things up
// . return ptr to the start of the line in case it has "tag:" i guess
char *getMatchingUrlPattern ( SpiderColl *sc , 
			      SpiderRequest *sreq ,
			      char *tagArg ) { // tagArg can be NULL

	// if it has * and no negatives, we are in!
	//if ( sc->m_siteListAsteriskLine && ! sc->m_siteListHasNegatives )
	//	return sc->m_siteListAsteriskLine;

	// if it is just a bunch of comments or blank lines, it is empty
	if ( sc->m_siteListIsEmpty && sc->m_siteListIsEmptyValid )
		return NULL;

	// if we had a list of contains: or regex: directives in the sitelist
	// we have to linear scan those
	char *nb = sc->m_negSubstringBuf.getBufStart();
	char *nbend = nb + sc->m_negSubstringBuf.getLength();
	for ( ; nb && nb < nbend ; ) {
		// return NULL if matches a negative substring
		if ( strstr ( sreq->m_url , nb ) ) return NULL;
		// skip it
		nb += strlen(nb) + 1;
	}


	char *myPath = NULL;

	// check domain specific tables
	HashTableX *dt = &sc->m_siteListDomTable;

	// get this
	CollectionRec *cr = sc->getCollectionRec();

	// need to build dom table for pattern matching?
	if ( dt->getNumSlotsUsed() == 0 && cr ) {
		// do not add seeds, just make siteListDomTable, etc.
		updateSiteListBuf ( sc->m_collnum , 
				       false , // add seeds?
				       cr->m_siteListBuf.getBufStart() );
	}

	if ( dt->getNumSlotsUsed() == 0 ) { 
		// empty site list -- no matches
		return NULL;
		//char *xx=NULL;*xx=0; }
	}

	// this table maps a 32-bit domain hash of a domain to a
	// patternData class. only for those urls that have firstIps that
	// we handle.
	int32_t slot = dt->getSlot ( &sreq->m_domHash32 );

	char *buf = cr->m_siteListBuf.getBufStart();

	// loop over all the patterns that contain this domain and see
	// the first one we match, and if we match a negative one.
	for ( ; slot >= 0 ; slot = dt->getNextSlot(slot,&sreq->m_domHash32)) {
		// get pattern
		PatternData *pd = (PatternData *)dt->getValueFromSlot ( slot );
		// point to string
		char *patternStr = buf + pd->m_patternStrOff;
		// is it negative? return NULL if so so url will be ignored
		//if ( patternStr[0] == '-' ) 
		//	return NULL;
		// otherwise, it has a path. skip if we don't match path ptrn
		if ( pd->m_pathOff ) {
			if ( ! myPath ) myPath = sreq->getUrlPath();
			if ( strncmp (myPath,
				      patternStr + pd->m_pathOff,
				      pd->m_pathLen ) )
				continue;
		}

		// for entries like http://domain.com/ we have to match
		// protocol and url can NOT be like www.domain.com to match.
		// this is really like a regex like ^http://xyz.com/poo/boo/
		if ( (patternStr[0]=='h' ||
		      patternStr[0]=='H') &&
		     ( patternStr[1]=='t' ||
		       patternStr[1]=='T' ) &&
		     ( patternStr[2]=='t' ||
		       patternStr[2]=='T' ) &&
		     ( patternStr[3]=='p' ||
		       patternStr[3]=='P' ) ) {
			char *x = patternStr+4;
			// is it https:// ?
			if ( *x == 's' || *x == 'S' ) x++;
			// watch out for subdomains like http.foo.com
			if ( *x != ':' ) goto nomatch;
			// ok, we have to substring match exactly. like 
			// ^http://xyssds.com/foobar/
			char *a = patternStr;
			char *b = sreq->m_url;
			for ( ; ; a++, b++ ) {
				// stop matching when pattern is exhausted
				if ( is_wspace_a(*a) || ! *a ) 
					return patternStr;
				if ( *a != *b ) break;
			}
			// we failed to match "pd" so try next line
			continue;
		}
 nomatch:		


		// if caller also gave a tag we'll want to see if this
		// "pd" has an entry for this domain that has that tag
		if ( tagArg ) {
			// skip if entry has no tag
			if ( pd->m_tagLen <= 0 ) continue;
			// skip if does not match domain or host
			if ( pd->m_thingHash32 != sreq->m_domHash32 &&
			     pd->m_thingHash32 != sreq->m_hostHash32 )
				continue;
			// compare tags
			char *pdtag = pd->m_tagOff + buf;
			if ( strncmp(tagArg,pdtag,pd->m_tagLen) ) continue;
			// must be nothing after
			if ( is_alnum_a(tagArg[pd->m_tagLen]) ) continue;
			// that's a match
			return patternStr;
		}

		// was the line just a domain and not a subdomain?
		if ( pd->m_thingHash32 == sreq->m_domHash32 )
			// this will be false if negative pattern i guess
			return patternStr;
		// was it just a subdomain?
		if ( pd->m_thingHash32 == sreq->m_hostHash32 )
			// this will be false if negative pattern i guess
			return patternStr;
	}


	// if we had a list of contains: or regex: directives in the sitelist
	// we have to linear scan those
	char *pb = sc->m_posSubstringBuf.getBufStart();
	char *pend = pb + sc->m_posSubstringBuf.length();
	for ( ; pb && pb < pend ; ) {
		// return NULL if matches a negative substring
		if ( strstr ( sreq->m_url , pb ) ) return pb;
		// skip it
		pb += strlen(pb) + 1;
	}


	// is there an '*' in the patterns?
	//if ( sc->m_siteListAsteriskLine ) return sc->m_siteListAsteriskLine;

	return NULL;
}

bool printSitePatternExamples ( SafeBuf *sb , HttpRequest *hr ) {

	// true = useDefault?
	CollectionRec *cr = g_collectiondb.getRec ( hr , true );
	if ( ! cr ) return true;

	/*
	// it is a safebuf parm
	char *siteList = cr->m_siteListBuf.getBufStart();
	if ( ! siteList ) siteList = "";

	SafeBuf msgBuf;
	char *status = "";
	int32_t max = 1000000;
	if ( cr->m_siteListBuf.length() > max ) {
		msgBuf.safePrintf( "<font color=red><b>"
				   "Site list is over %"INT32" bytes large, "
				   "too many to "
				   "display on this web page. Please use the "
				   "file upload feature only for now."
				   "</b></font>"
				   , max );
		status = " disabled";
	}
	*/


	/*
	sb->safePrintf(
		       "On the command like you can issue a command like "

		       "<i>"
		       "gb addurls &lt; fileofurls.txt"
		       "</i> or "

		       "<i>"
		       "gb addfile &lt; *.html"
		       "</i> or "

		       "<i>"
		       "gb injecturls &lt; fileofurls.txt"
		       "</i> or "

		       "<i>"
		       "gb injectfile &lt; *.html"
		       "</i> or "

		       "to schedule downloads or inject content directly "
		       "into Gigablast."

		       "</td><td>"

		       "<input "
		       "size=20 "
		       "type=file "
		       "name=urls>"
		       "</td></tr>"

		       );
	*/	      

	// example table
	sb->safePrintf ( "<a name=examples></a>"
			 "<table %s>"
			 "<tr class=hdrow><td colspan=2>"
			 "<center><b>Site List Examples</b></tr></tr>"
			 //"<tr bgcolor=#%s>"
			 //"<td>"
			 ,TABLE_STYLE );//, DARK_BLUE);
			 

	sb->safePrintf(
		       //"*"
		       //"</td>"
		       //"<td>Spider all urls encountered. If you just submit "
		       //"this by itself, then Gigablast will initiate spidering "
		       //"automatically at dmoz.org, an internet "
		      //"directory of good sites.</td>"
		       //"</tr>"

		      "<tr>"
		      "<td>goodstuff.com</td>"
		      "<td>"
		      "Spider the url <i>goodstuff.com/</i> and spider "
		      "any links we harvest that have the domain "
		      "<i>goodstuff.com</i>"
		      "</td>"
		      "</tr>"

		      // protocol and subdomain match
		      "<tr>"
		      "<td>http://www.goodstuff.com/</td>"
		      "<td>"
		      "Spider the url "
		      "<i>http://www.goodstuff.com/</i> and spider "
		      "any links we harvest that start with "
		      "<i>http://www.goodstuff.com/</i>. NOTE: if the url "
		      "www.goodstuff.com redirects to foo.goodstuff.com then "
		      "foo.goodstuff.com still gets spidered "
		      "because it is considered to be manually added, but "
		      "no other urls from foo.goodstuff.com will be spidered."
		      "</td>"
		      "</tr>"

		      // protocol and subdomain match
		      "<tr>"
		      "<td>http://justdomain.com/foo/</td>"
		      "<td>"
		      "Spider the url "
		      "<i>http://justdomain.com/foo/</i> and spider "
		      "any links we harvest that start with "
		      "<i>http://justdomain.com/foo/</i>. "
		      "Urls that start with "
		      "<i>http://<b>www.</b>justdomain.com/</i>, for example, "
		      "will NOT match this."
		      "</td>"
		      "</tr>"

		      "<tr>"
		      "<td>seed:www.goodstuff.com/myurl.html</td>"
		      "<td>"
		      "Spider the url <i>www.goodstuff.com/myurl.html</i>. "
		      "Add any outlinks we find into the "
		      "spider queue, but those outlinks will only be "
		      "spidered if they "
		      "match ANOTHER line in this site list."
		      "</td>"
		      "</tr>"


		      // protocol and subdomain match
		      "<tr>"
		      "<td>site:http://www.goodstuff.com/</td>"
		      "<td>"
		      "Allow any urls starting with "
		      "<i>http://www.goodstuff.com/</i> to be spidered "
		      "if encountered."
		      "</td>"
		      "</tr>"

		      // subdomain match
		      "<tr>"
		      "<td>site:www.goodstuff.com</td>"
		      "<td>"
		      "Allow any urls starting with "
		      "<i>www.goodstuff.com/</i> to be spidered "
		      "if encountered."
		      "</td>"
		      "</tr>"

		      "<tr>"
		      "<td>-site:bad.goodstuff.com</td>"
		      "<td>"
		      "Do not spider any urls starting with "
		      "<i>bad.goodstuff.com/</i> to be spidered "
		      "if encountered."
		      "</td>"
		      "</tr>"

		      // domain match
		      "<tr>"
		      "<td>site:goodstuff.com</td>"
		      "<td>"
		      "Allow any urls starting with "
		      "<i>goodstuff.com/</i> to be spidered "
		      "if encountered."
		      "</td>"
		      "</tr>"

		      // spider this subdir
		      "<tr>"
		      "<td><nobr>site:"
		      "http://www.goodstuff.com/goodir/anotherdir/</nobr></td>"
		      "<td>"
		      "Allow any urls starting with "
		      "<i>http://www.goodstuff.com/goodir/anotherdir/</i> "
		      "to be spidered "
		      "if encountered."
		      "</td>"
		      "</tr>"


		      // exact match
		      
		      //"<tr>"
		      //"<td>exact:http://xyz.goodstuff.com/myurl.html</td>"
		      //"<td>"
		      //"Allow this specific url."
		      //"</td>"
		      //"</tr>"

		      /*
		      // local subdir match
		      "<tr>"
		      "<td>file://C/mydir/mysubdir/"
		      "<td>"
		      "Spider all files in the given subdirectory or lower. "
		      "</td>"
		      "</tr>"

		      "<tr>"
		      "<td>-file://C/mydir/mysubdir/baddir/"
		      "<td>"
		      "Do not spider files in this subdirectory."
		      "</td>"
		      "</tr>"
		      */

		      // connect to a device and index it as a stream
		      //"<tr>"
		      //"<td>stream:/dev/eth0"
		      //"<td>"
		      //"Connect to a device and index it as a stream. "
		      //"It will be treated like a single huge document for "
		      //"searching purposes with chunks being indexed in "
		      //"realtime. Or chunk it up into individual document "
		      //"chunks, but proximity term searching will have to "
		      //"be adjusted to compute query term distances "
		      //"inter-document."
		      //"</td>"
		      //"</tr>"

		      // negative subdomain match
		      "<tr>"
		      "<td>contains:goodtuff</td>"
		      "<td>Spider any url containing <i>goodstuff</i>."
		      "</td>"
		      "</tr>"

		      "<tr>"
		      "<td>-contains:badstuff</td>"
		      "<td>Do not spider any url containing <i>badstuff</i>."
		      "</td>"
		      "</tr>"

		      /*
		      "<tr>"
		      "<td>regexp:-pid=[0-9A-Z]+/</td>"
		      "<td>Url must match this regular expression. "
		      "Try to avoid using these if possible; they can slow "
		      "things down and are confusing to use."
		      "</td>"
		      "</tr>"
		      */

		      // tag match
		      "<tr><td>"
		      //"<td>tag:boots contains:boots<br>"
		      "<nobr>tag:boots site:www.westernfootwear."
		      "</nobr>com<br>"
		      "tag:boots cowboyshop.com<br>"
		      "tag:boots contains:/boots<br>"
		      "tag:boots site:www.moreboots.com<br>"
		      "<nobr>tag:boots http://lotsoffootwear.com/"
		      "</nobr><br>"
		      //"<td>t:boots -contains:www.cowboyshop.com/shoes/</td>"
		      "</td><td>"
		      "Advance users only. "
		      "Tag any urls matching these 5 url patterns "
		      "so we can use "
		      "the expression <i>tag:boots</i> in the "
		      "<a href=/admin/filters>url filters</a> and perhaps "
		      "give such urls higher spider priority. "
		      "For more "
		      "precise spidering control over url subsets. "
		      "Preceed any pattern with the tagname followed by "
		      "space to tag it."
		      "</td>"
		      "</tr>"


		      "<tr>"
		      "<td># This line is a comment.</td>"
		      "<td>Empty lines and lines starting with # are "
		      "ignored."
		      "</td>"
		      "</tr>"

		      "</table>"
		      );

	return true;
}

bool printScrollingWidget ( SafeBuf *sb , CollectionRec *cr ) {

	sb->safePrintf("<script type=\"text/javascript\">\n\n");

	// if user has the scrollbar at the top
	// in the widget we do a search every 15 secs
	// to try to load more recent results. we should
	// return up to 10 results above your last 
	// top docid and 10 results below it. that way
	// no matter which of the 10 results you were
	// viewing your view should remaing unchanged.
	sb->safePrintf(

		       // global var
		       "var forcing;"

		       "function widget123_handler_reload() {"
		       // return if reply is not fully ready
		       "if(this.readyState != 4 )return;"

		       // if error or empty reply then do nothing
		       "if(!this.responseText)return;"
		       // get the widget container
		       "var w=document.getElementById(\"widget123\");"

		       // GET DOCID of first div/searchresult
		       "var sd=document.getElementById("
		       "\"widget123_scrolldiv\");"
		       "var cd;"
		       "if ( sd ) cd=sd.firstChild;"
		       "var fd=0;"
		       // if nodetype is 3 that means it says
		       // 'No results. Waiting for spider to kick in.'
		       "if(cd && cd.nodeType==1) fd=cd.getAttribute('docid');"

		       // if the searchbox has the focus then do not
		       // update the content just yet...
		       "var qb=document.getElementById(\"qbox\");"
		       "if(qb&&qb==document.activeElement)"
		       "return;"

		       //"alert(this.responseText);"

		       // or if not forced and they scrolled down
		       // don't jerk them back up again. unless
		       // the inner html starts with 'No results'!
		       "if(!forcing&&sd&&sd.scrollTop!=0&&cd&&cd.nodeType==1)"
		       "return;"


		       // just set the widget content to the reply
		       "w.innerHTML=this.responseText;"

		       //
		       // find that SAME docid in response and see
		       // how many new results were added above it
		       //
		       "var added=0;"
		       // did we find the docid?
		       "var found=0;"
		       // get div again since we updated innerHTML
		       "sd=document.getElementById("
		       "\"widget123_scrolldiv\");"
		       // scan the kids
		       "var kid=sd.firstChild;"
		       // begin the while loop to scan the kids
		       "while (kid) {"
		       // if div had no docid it might have been a line
		       // break div, so ignore
		       "if (!kid.hasAttribute('docid') ) {"
		       "kid=kid.nextSibling;"
		       "continue;"
		       "}"
		       // set kd to docid of kid
		       "var kd=kid.getAttribute('docid');"
		       // stop if we hit our original top docid
		       "if(kd==fd) {found=1;break;}"
		       // otherwise count it as a NEW result we got
		       "added++;"
		       // advance kid
		       "kid=kid.nextSibling;"
		       // end while loop
		       "}"

		       //"alert(\"added=\"+added);"

		       // how many results did we ADD above the
		       // reported "topdocid" of the widget?
		       // it should be in the ajax reply from the
		       // search engine. how many result were above
		       // the given "topdocid".
		       //"var ta=document.getElementById(\"topadd\");"
		       //"var added=0;"
		       //"if(ta)added=ta.value;"

		       // if nothing added do nothing
		       "if (added==0)return;"

		       // if original top docid not found, i guess we
		       // added too many new guys to the top of the
		       // search results, so don't bother scrolling
		       // just reset to top
		       "if (!found) return;"

		       // show that
		       //"alert(this.responseText);"

		       // get the div that has the scrollbar
		       "var sd=document.getElementById("
		       "\"widget123_scrolldiv\");"
		       // save current scroll pos
		       "var oldpos=parseInt(sd.scrollTop);"
		       // note it
		       //"alert (sd.scrollTop);"
		       // preserve the relative scroll position so we
		       // do not jerk around since we might have added 
		       // "added" new results to the top.
		       "sd.scrollTop += added*%"INT32";"

		       // try to scroll out new results if we are
		       // still at the top of the scrollbar and
		       // there are new results to scroll.
		       "if(oldpos==0)widget123_scroll();}\n\n"

		       // for preserving scrollbar position
		       ,(int32_t)RESULT_HEIGHT +2*PADDING

		       );


	// scroll the widget up until we hit the 0 position
	sb->safePrintf(
		       "function widget123_scroll() {"
		       // only scroll if at the top of the widget
		       // and not scrolled down so we do not
		       // interrupt
		       "var sd=document.getElementById("
		       "\"widget123_scrolldiv\");"
		       // TODO: need parseInt here?
		       "var pos=parseInt(sd.scrollTop);"
		       // note it
		       //"alert (sd.scrollTop);"
		       // if already at the top of widget, return
		       "if(pos==0)return;"
		       // decrement by 3 pixels
		       "pos=pos-3;"
		       // do not go negative
		       "if(pos<0)pos=0;"
		       // assign to scroll up. TODO: need +\"px\"; ?
		       "sd.scrollTop=pos;"
		       // all done, then return
		       "if(pos==0) return;"
		       // otherwise, scroll more in 3ms
		       // TODO: make this 1000ms on result boundaries
		       // so it delays on each new result. perhaps make
		       // it less than 1000ms if we have a lot of 
		       // results above us!
		       "setTimeout('widget123_scroll()',3);}\n\n"

		       );

	// this function appends the search results to what is
	// already in the widget.
	sb->safePrintf(
		       "function widget123_handler_append() {"
		       // return if reply is not fully ready
		       "if(this.readyState != 4 )return;"
		       // i guess we are done... release the lock
		       "outstanding=0;"
		       // if error or empty reply then do nothing
		       "if(!this.responseText)return;"
		       // if too small
		       "if(this.responseText.length<=3)return;"
		       // get the widget container
		       "var w=document.getElementById("
		       "\"widget123_scrolldiv\");"
		       // just set the widget content to the reply
		       "w.innerHTML+=this.responseText;"
		       "}\n\n"
		       );


	//sb->safePrintf ( "</script>\n\n" );

	int32_t widgetWidth = 300;
	int32_t widgetHeight = 500;

	// make the ajax url that gets the search results
	SafeBuf ub;
	ub.safePrintf("/search"
		      //"format=ajax"
		      "?c=%s"
		      //"&prepend=gbsortbyint%%3Agbspiderdate"
		      "&q=-gbstatus:0+gbsortbyint%%3Agbindexdate"
		      "&sc=0" // no site clustering
		      "&dr=0" // no deduping
			      // 10 results at a time
		      "&n=10"
		      "&widgetheight=%"INT32""
		      "&widgetwidth=%"INT32""
		      , cr->m_coll
		      , widgetHeight
		      , widgetWidth
		      );
	//ub.safePrintf("&topdocid="
	//	      );

	// get the search results from neo as soon as this div is
	// being rendered, and set its contents to them
	sb->safePrintf(//"<script type=text/javascript>"

		       "function widget123_reload(force) {"
			 
		       // when the user submits a new query in the
		       // query box we set force to false when
		       // we call this (see PageResults.cpp) so that
		       // we do not register multiple timeouts
		       "if ( ! force ) "
		       "setTimeout('widget123_reload(0)',%"INT32");"

		       // get the query box
		       "var qb=document.getElementById(\"qbox\");"

		       // if forced then turn off focus for searchbox
		       // since it was either 1) the initial call
		       // or 2) someone submitted a query and
		       // we got called from PageResults.cpp
		       // onsubmit event.
		       "if (force&&qb) qb.blur();"


		       // if the searchbox has the focus then do not
		       // reload!! unless force is true..
		       "if(qb&&qb==document.activeElement&&!force)"
		       "return;"

		       //"var ee=document.getElementById(\"sbox\");"
		       //"if (ee)alert('reloading '+ee.style.display);"

		       // do not do timer reload if searchbox is
		       // visible because we do not want to interrupt
		       // a possible search
		       //"if(!force&&ee && ee.style.display=='')return;"


		       // do not bother timed reloading if scrollbar pos
		       // not at top or near bottom
		       "var sd=document.getElementById("
		       "\"widget123_scrolldiv\");"

		       "if ( sd && !force ) {"
		       "var pos=parseInt(sd.scrollTop);"
		       "if (pos!=0) return;"
		       "}"


		       "var client=new XMLHttpRequest();"
		       "client.onreadystatechange="
		       "widget123_handler_reload;"

		       // . this url gets the search results
		       // . get them in "ajax" format so we can embed
		       //   them into the base html as a widget
		       "var u='%s&format=ajax';"

		       // append our query from query box if there
		       "var qv;"
		       "if (qb) qv=qb.value;"
		       "if (qv){"
		       //"u+='&q=';"
		       "u+='&prepend=';"
		       "u+=encodeURI(qv);"
		       "}"

		       // set global var so handler knows if we were
		       // forced or not
		       "forcing=force;"

		       // get the docid at the top of the widget
		       // so we can get SURROUNDING search results,
		       // like 10 before it and 10 after it for
		       // our infinite scrolling
		       //"var td=document.getElementById('topdocid');"
		       //"if ( td ) u=u+\"&topdocid=\"+td.value;"

		       //"alert('reloading');"

		       "client.open('GET',u);"
		       "client.send();"
		       "}\n\n"

		       // when page loads, populate the widget immed.
		       "widget123_reload(1);\n\n"

		       // initiate the timer loop since it was
		       // not initiated on that call since we had to
		       // set force=1 to load in case the query box
		       // was currently visible.
		       "setTimeout('widget123_reload(0)',%"INT32");"

		       //, widgetHeight
		       , (int32_t)DEFAULT_WIDGET_RELOAD
		       , ub.getBufStart()
		       , (int32_t)DEFAULT_WIDGET_RELOAD
		       );

	//
	// . call this when scrollbar gets 5 up from bottom
	// . but if < 10 new results are appended, then stop!
	//
	sb->safePrintf(
		       "var outstanding=0;\n\n"

		       "function widget123_append() {"
			      
		       // bail if already outstanding
		       "if (outstanding) return;"

		       // if scrollbar not near bottom, then return
		       "var sd=document.getElementById("
		       "\"widget123_scrolldiv\");"
		       "if ( sd ) {"
		       "var pos=parseInt(sd.scrollTop);"
		       "if (pos < (sd.scrollHeight-%"INT32")) "
		       "return;"
		       "}"

		       // . this url gets the search results
		       // . just get them so we can APPEND them to
		       //   the widget, so it will be just the
		       //   "results" divs
		       "var u='%s&format=append';"

		       // . get score of the last docid in our widget
		       // . it should be persistent.
		       // . it is like a bookmark for scrolling
		       // . append results AFTER it into the widget
		       // . this way we can deal with the fact that
		       //   we may be adding 100s of results to this
		       //   query per second, especially if spidering
		       //   at a high rate. and this will keep the
		       //   results we append persistent.
		       // . now we scan the children "search result"
		       //   divs of the "widget123_scrolldiv" div
		       //   container to get the last child and get
		       //   its score/docid so we can re-do the search
		       //   and just get the search results with
		       //   a score/docid LESS THAN that. THEN our
		       //   results should be contiguous.
		       // . get the container div, "cd"
		       "var cd=document.getElementById("
		       "'widget123_scrolldiv');"
		       // must be there
		       "if(!cd)return;"
		       // get the last child div in there
		       "var d=cd.lastChild.previousSibling;"
		       // must be there
		       "if(!d)return;"
		       // now that we added <hr> tags after each div do this!
		       "d=d.previousSibling;"
		       // must be there
		       "if(!d)return;"
		       // get docid/score
		       "u=u+\"&maxserpscore=\"+d.getAttribute('score');"
		       "u=u+\"&minserpdocid=\"+d.getAttribute('docid');"

		       // append our query from query box if there
		       "var qb=document.getElementById(\"qbox\");"
		       "var qv;"
		       "if (qb) qv=qb.value;"
		       "if (qv){"
		       //"u+='&q=';"
		       "u+='&prepend=';"
		       "u+=encodeURI(qv);"
		       "}"


		       // turn on the lock to prevent excessive calls
		       "outstanding=1;"

		       //"alert(\"scrolling2 u=\"+u);"

		       "var client=new XMLHttpRequest();"
		       "client.onreadystatechange="
		       "widget123_handler_append;"

		       //"alert('appending scrollTop='+sd.scrollTop+' scrollHeight='+sd.scrollHeight+' 5results=%"INT32"'+u);"
		       "client.open('GET',u);"
		       "client.send();"
		       "}\n\n"

		       "</script>\n\n"

		       // if (pos < (sd.scrollHeight-%"INT32")) return...
		       // once user scrolls down to within last 5
		       // results then try to append to the results.
		       , widgetHeight +5*((int32_t)RESULT_HEIGHT+2*PADDING)


		       , ub.getBufStart()

		       //,widgetHeight +5*((int32_t)RESULT_HEIGHT+2*PADDING
		       );


	// then the WIDGET MASTER div. set the "id" so that the
	// style tag the user sets can control its appearance.
	// when the browser loads this the ajax sets the contents
	// to the reply from neo.

	// on scroll call widget123_append() which will append
	// more search results if we are near the bottom of the
	// widget.

	sb->safePrintf("<div id=widget123 "
		       "style=\"border:2px solid black;"
		       "position:relative;border-radius:10px;"
		       "width:%"INT32"px;height:%"INT32"px;\">"
		       , widgetWidth
		       , widgetHeight
		       );

	//sb->safePrintf("<style>"
	//	      "a{color:white;}"
	//	      "</style>");


	sb->safePrintf("Waiting for Server Response...");


	// end the containing div
	sb->safePrintf("</div>");

	return true;
}

bool sendPageWidgets ( TcpSocket *socket , HttpRequest *hr ) {

	// true = usedefault coll?
	CollectionRec *cr = g_collectiondb.getRec ( hr , true );
	if ( ! cr ) {
		g_httpServer.sendErrorReply(socket,500,"invalid collection");
		return true;
	}

	char  buf [ 128000 ];
	SafeBuf sb(buf,128000);

	printFrontPageShell ( &sb, "widgets", cr , true );

	sb.safePrintf("<br>");
	sb.safePrintf("<br>");

	//char format = hr->getReplyFormat();
	//if ( format == FORMAT_HTML )
	printGigabotAdvice ( &sb , PAGE_BASIC_STATUS , hr , NULL );

	printScrollingWidget ( &sb , cr );

	return g_httpServer.sendDynamicPage (socket, 
					     sb.getBufStart(), 
					     sb.length(),
					     0); // cachetime
}



// from pagecrawlbot.cpp for printCrawlDetails()
#include "PageCrawlBot.h"

///////////
//
// main > Basic > Status
//
///////////
bool sendPageBasicStatus ( TcpSocket *socket , HttpRequest *hr ) {

	char  buf [ 128000 ];
	SafeBuf sb(buf,128000);
	sb.reset();

	// char *fs = hr->getString("format",NULL,NULL);
	// char format = FORMAT_HTML;
	// if ( fs && strcmp(fs,"html") == 0 ) format = FORMAT_HTML;
	// if ( fs && strcmp(fs,"json") == 0 ) format = FORMAT_JSON;
	// if ( fs && strcmp(fs,"xml") == 0 ) format = FORMAT_XML;
	char format = hr->getReplyFormat();


	// true = usedefault coll?
	CollectionRec *cr = g_collectiondb.getRec ( hr , true );
	if ( ! cr ) {
		g_httpServer.sendErrorReply(socket,500,"invalid collection");
		return true;
	}

	if ( format == FORMAT_JSON || format == FORMAT_XML) {
		// this is in PageCrawlBot.cpp
		printCrawlDetails2 ( &sb , cr , format );
		char *ct = "text/xml";
		if ( format == FORMAT_JSON ) ct = "application/json";
		return g_httpServer.sendDynamicPage (socket, 
						     sb.getBufStart(), 
						     sb.length(),
						     0, // cachetime
						     false,//POSTReply        ,
						     ct);
	}

	// print standard header 
	if ( format == FORMAT_HTML )
		// this prints the <form tag as well
		g_pages.printAdminTop ( &sb , socket , hr );

	// table to split between widget and stats in left and right panes
	if ( format == FORMAT_HTML ) {
		sb.safePrintf("<TABLE id=pane>"
			      "<TR><TD valign=top>");
	}

	int32_t savedLen1, savedLen2;

	//
	// widget
	//
	// put the widget in here, just sort results by spidered date
	//
	// the scripts do "infinite" scrolling both up and down.
	// but if you are at the top then new results will load above
	// you and we try to maintain your current visual state even though
	// the scrollbar position will change.
	//
	if ( format == FORMAT_HTML ) {

		// save position so we can output the widget code
		// so user can embed it into their own web page
		savedLen1 = sb.length();
		
		printScrollingWidget ( &sb , cr );

		savedLen2 = sb.length();

	}

	// the right table pane is the crawl stats
	if ( format == FORMAT_HTML ) {
		sb.safePrintf("</TD><TD valign=top>");
	}


	//
	// show stats
	//
	if ( format == FORMAT_HTML ) {

		char *seedStr = cr->m_diffbotSeeds.getBufStart();
		if ( ! seedStr ) seedStr = "";

		SafeBuf tmp;
		int32_t crawlStatus = -1;
		getSpiderStatusMsg ( cr , &tmp , &crawlStatus );
		CrawlInfo *ci = &cr->m_localCrawlInfo;
		int32_t sentAlert = (int32_t)ci->m_sentCrawlDoneAlert;
		if ( sentAlert ) sentAlert = 1;

		//sb.safePrintf(
		//	      "<form method=get action=/crawlbot>"
		//	      "%s"
		//	      , sb.getBufStart() // hidden input token/name/..
		//	      );

		char *hurts = "No";
		if ( cr->m_globalCrawlInfo.m_hasUrlsReadyToSpider )
			hurts = "Yes";

		sb.safePrintf(//"<TABLE border=0>"
			      //"<TR><TD valign=top>"

			      "<table id=stats border=0 cellpadding=5>"

			      "<tr>"
			      "<td><b>Crawl Status Code:</td>"
			      "<td>%"INT32"</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Crawl Status Msg:</td>"
			      "<td>%s</td>"
			      "</tr>"

			      //"<tr>"
			      //"<td><b>Rounds Completed:</td>"
			      //"<td>%"INT32"</td>"
			      //"</tr>"

			      "<tr>"
			      "<td><b>Has Urls Ready to Spider:</td>"
			      "<td>%s</td>"
			      "</tr>"


			      // this will  have to be in crawlinfo too!
			      //"<tr>"
			      //"<td><b>pages indexed</b>"
			      //"<td>%"INT64"</td>"
			      //"</tr>"

			      "<tr>"
			      "<td><b><nobr>URLs Harvested</b> "
			      "(may include dups)</nobr></td>"
			      "<td>%"INT64"</td>"
     
			      "</tr>"

			      //"<tr>"
			      //"<td><b>URLs Examined</b></td>"
			      //"<td>%"INT64"</td>"
			      //"</tr>"

			      "<tr>"
			      "<td><b>Page Crawl Attempts</b></td>"
			      "<td>%"INT64"</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Page Crawl Successes</b></td>"
			      "<td>%"INT64"</td>"
			      "</tr>"
			      , crawlStatus
			      , tmp.getBufStart()
			      //, cr->m_spiderRoundNum
			      //, cr->m_globalCrawlInfo.m_hasUrlsReadyToSpider
			      , hurts

			      , cr->m_globalCrawlInfo.m_urlsHarvested
			      //, cr->m_globalCrawlInfo.m_urlsConsidered

			      , cr->m_globalCrawlInfo.m_pageDownloadAttempts
			      , cr->m_globalCrawlInfo.m_pageDownloadSuccesses
			      );


		//
		// begin status code breakdown
		//
		for ( int32_t i = 0 ; i < 65536 ; i++ ) {
			if ( g_stats.m_allErrorsNew[i] == 0 &&
			     g_stats.m_allErrorsOld[i] == 0 )
				continue;
			sb.safePrintf (
				       "<tr>"
				       "<td><b> &nbsp; <a href=/search?c=%s&q="
				       "gbstatusmsg%%3A"
				       "%%22"
				       ,
				       cr->m_coll );
			sb.urlEncode(mstrerror(i));
			sb.safePrintf ("%%22>"
				       "%s"
				       "</a>"
				       "</b></td>"
				       "<td>%"INT64"</td>"
				       "</tr>\n" ,
				       mstrerror(i),
				       g_stats.m_allErrorsNew[i] +
				       g_stats.m_allErrorsOld[i] );
		}
		//
		// end status code breakdown
		//


		char tmp3[64];
		struct tm *timeStruct;
		time_t tt = (time_t)cr->m_diffbotCrawlStartTime;
		timeStruct = localtime(&tt);
		// Jan 01 1970 at 10:30:00
		strftime ( tmp3,64 , "%b %d %Y at %H:%M:%S",timeStruct);
		sb.safePrintf("<tr><td><b>Collection Created</b></td>"
			      "<td>%s (local time)</td></tr>",tmp3);



		
		// print link to embed the code in their own site
		SafeBuf embed;
		embed.htmlEncode(sb.getBufStart()+savedLen1,
				 savedLen2-savedLen1,
				 false); // encodePoundSign #?
		// convert all ''s to "'s for php's echo ''; cmd
		embed.replaceChar('\'','\"');

		sb.safePrintf("<tr>"
			      "<td valign=top>"
			      "<a onclick=\""
			      "var dd=document.getElementById('hcode');"
			      "if ( dd.style.display=='none' ) "
			      "dd.style.display=''; "
			      "else "
			      "dd.style.display='none';"
			      "\" style=color:blue;>"
			      "<u>"
			      "show Widget HTML code"
			      "</u>"
			      "</a>"
			      "</td><td>"
			      "<div id=hcode style=display:none;"
			      "max-width:800px;>"
			      "%s"
			      "</div>"
			      "</td></tr>"
			      , embed.getBufStart() );

		sb.safePrintf("<tr>"
			      "<td valign=top>"
			      "<a onclick=\""
			      "var dd=document.getElementById('pcode');"
			      "if ( dd.style.display=='none' ) "
			      "dd.style.display=''; "
			      "else "
			      "dd.style.display='none';"
			      "\" style=color:blue;>"
			      "<u>"
			      "show Widget PHP code"
			      "</u>"
			      "</a>"
			      "</td>"
			      "<td>"
			      "<div id=pcode style=display:none;"
			      "max-width:800px;>"
			      "<i>"
			      "echo '"
			      "%s"
			      "';"
			      "</i>"
			      "</div>"
			      "</td></tr>"
			      , embed.getBufStart() );


		sb.safePrintf("</table>\n\n");

	}

	// end the right table pane
	if ( format == FORMAT_HTML ) {
		sb.safePrintf("</TD></TR></TABLE>");
	}


	//if ( format != FORMAT_JSON )
	//	// wrap up the form, print a submit button
	//	g_pages.printAdminBottom ( &sb );

	return g_httpServer.sendDynamicPage (socket, 
					     sb.getBufStart(), 
					     sb.length(),
					     0); // cachetime
}
	
