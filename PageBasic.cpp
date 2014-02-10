#include "SafeBuf.h"
#include "HttpRequest.h"
#include "SearchInput.h"
#include "Pages.h"
#include "Parms.h"
#include "Spider.h"

bool printExampleTable ( SafeBuf *sb , HttpRequest *hr ) ;

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

	// . print standard header 
	// . this prints the <form tag as well
	g_pages.printAdminTop ( &sb , socket , hr );


	g_parms.printParms ( &sb , socket , hr );

	
	printExampleTable ( &sb , hr );

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
	long m_thingHash32;
	// ptr to the line in CollectionRec::m_siteListBuf
	char *m_patternStr;
	// offset of the url path in the pattern, 0 means none
	short m_pathOff; 
	short m_pathLen;
};


// . CommandUpdateSiteList() should call this
// . this returns false if it blocks
// . returns true and sets g_errno on error
// . uses msg4 to add seeds to spiderdb if necessary
// . only adds seeds for the shard we are on iff we are responsible for
//   the fake firstip!!!
bool updateSiteList ( collnum_t collnum , char *siteList ) {

	CollectionRec *cr = g_collectiondb.getRec ( collnum );
	if ( ! cr ) return true;

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
	for ( ; *op ; op++ ) {
		// get end
		char *s = op;
		// skip to end of line marker
		for ( ; *op && *op != '\n' ; op++ ) ;
		// keep it simple
		long h32 = hash32 ( s , op - s );
		// for deduping
		if ( ! dedup.addKey ( &h32 ) ) return true;
	}

	// get the old sitelist Domain Hash to PatternData mapping table
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


	// we can now free the old site list methinks
	cr->m_siteListBuf.purge();

	// reset flags
	sc->m_siteListAsteriskLine = NULL;
	sc->m_siteListHasNegatives = false;
	sc->m_siteListIsEmpty = true;

	// use this so it will be free automatically when msg4 completes!
	SafeBuf *spiderReqBuf = &sc->m_msg4x.m_tmpBuf;

	// scan the list
	char *pn = siteList;

	// completely empty?
	if ( ! pn ) return true;

	long lineNum = 1;

	Url u;

	for ( ; *pn ; pn++ , lineNum++ ) {

		// get end
		char *s = pn;
		// skip to end of line marker
		for ( ; *pn && *pn != '\n' ; pn++ ) ;

		char *start = s;

		// back p up over spaces in case ended in spaces
	        char *pe = pn;
		for ( ; pe > s && pe[-1] == ' ' ; pe-- );

		// make hash of the line
		long h32 = hash32 ( s , pe - s );

		bool exact = false;

	innerLoop:
		// skip spaces at start of line
		if ( *s == ' ' ) s++;

		// comment?
		if ( *s == '#' ) continue;

		// empty line?
		if ( *s == '\n' ) continue;

		// all?
		if ( *s == '*' ) {
			sc->m_siteListAsteriskLine = start;
			continue;
		}

		if ( *s == '-' ) {
			sc->m_siteListHasNegatives = true;
			s++;
		}

		// exact:?
		if ( strncmp(s,"exact:",6) == 0 ) {
			exact = true;
			s += 6;
			goto innerLoop;
		}

		u.set ( s , pe - s );

		// error? skip it then...
		if ( u.getHostLen() <= 0 ) {
			log("basic: error on line #%li in sitelist",lineNum);
			continue;
		}

		// see if in existing table for existing site list
		if ( ! dedup.isInTable ( &h32 ) ) {
			// make spider request
			SpiderRequest sreq;
			sreq.setFromAddUrl ( u.getUrl() );
			// . add this url to spiderdb as a spiderrequest
			// . calling msg4 will be the last thing we do
			if(!spiderReqBuf->safeMemcpy(&sreq,sreq.getRecSize()))
				return true;
		}

		// make the data node
		PatternData pd;
		// hash of the subdomain or domain for this line in sitelist
		pd.m_thingHash32 = u.getHostHash32();
		// . ptr to the line in CollectionRec::m_siteListBuf. 
		// . includes pointing to "exact:" too i guess and tag: later.
		pd.m_patternStr = start;
		// offset of the url path in the pattern, 0 means none
		pd.m_pathOff = 0;
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
			if ( u.getPathLen() <= 1 ) { char *xx=NULL;*xx=0; }
			// calc length from "start" of line so we can
			// jump to the path quickly for compares
			pd.m_pathOff = x - start;
			pd.m_pathLen = pe - x;
			break;
		}

		// add to new dt
		long domHash32 = u.getDomainHash32();
		if ( ! dt->addKey ( &domHash32 , &pd ) )
			return true;

		// we have some patterns in there
		sc->m_siteListIsEmpty = false;
	}

	// go back to a high niceness
	dt->m_niceness = MAX_NICENESS;

	long siteListLen = gbstrlen(siteList);
	cr->m_siteListBuf.safeMemcpy ( siteList , siteListLen + 1 );


	// use spidercoll to contain this msg4 but if in use it
	// won't be able to be deleted until it comes back..
	if ( ! sc->m_msg4x.addMetaList ( spiderReqBuf ,
					 sc->m_collnum ,
					 // no need for callback since m_msg4x
					 // should set msg4::m_inUse to false
					 // when it comes back
					 NULL , // state
					 NULL , // callback 
					 MAX_NICENESS 
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
char *getMatchingUrlPattern ( SpiderColl *sc , SpiderRequest *sreq ) {

	// if it has * and no negatives, we are in!
	if ( sc->m_siteListAsteriskLine && ! sc->m_siteListHasNegatives )
		return sc->m_siteListAsteriskLine;

	// if it is just a bunch of comments or blank lines, it is empty
	if ( sc->m_siteListIsEmpty )
		return NULL;

	char *myPath = NULL;

	// check domain specific tables
	HashTableX *dt = &sc->m_siteListDomTable;

	// sanity check
	if ( dt->getNumSlotsUsed() == 0 ) { char *xx=NULL;*xx=0; }

	long slot = dt->getSlot ( &sreq->m_domHash32 );

	// loop over all the patterns that contain this domain and see
	// the first one we match, and if we match a negative one.
	for ( ; slot >= 0 ; slot = dt->getNextSlot(slot,&sreq->m_domHash32)) {
		// get pattern
		PatternData *pd = (PatternData *)dt->getValueFromSlot ( slot );
		// is it negative? return NULL if so so url will be ignored
		//if ( pd->m_patternStr[0] == '-' ) 
		//	return NULL;
		// otherwise, it has a path. skip if we don't match path ptrn
		if ( pd->m_pathOff ) {
			if ( ! myPath ) myPath = sreq->getUrlPath();
			if ( strncmp (myPath,
				      pd->m_patternStr + pd->m_pathOff,
				      pd->m_pathLen ) )
				continue;
		}
		// is it just a plain domain?
		if ( pd->m_thingHash32 == sreq->m_domHash32 )
			// this will be false if negative pattern i guess
			return pd->m_patternStr;
		// is it just a subdomain?
		if ( pd->m_thingHash32 == sreq->m_hostHash32 )
			// this will be false if negative pattern i guess
			return pd->m_patternStr;
	}

	// is there an '*' in the patterns?
	if ( sc->m_siteListAsteriskLine ) return sc->m_siteListAsteriskLine;

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
	long max = 1000000;
	if ( cr->m_siteListBuf.length() > max ) {
		msgBuf.safePrintf( "<font color=red><b>"
				   "Site list is over %li bytes large, "
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
			 "<center><b>Examples</b></tr></tr>"
			 "<tr bgcolor=#%s>"
			 "<td>"
			 ,TABLE_STYLE , DARK_BLUE);

	sb->safePrintf(
		      "*"
		      "</td>"
		      "<td>Spider all urls encountered. If you just submit "
		      "this by itself, then Gigablast will initiate spidering "
		      "automatically at dmoz.org, an internet "
		      "directory of good sites.</td>"
		      "</tr>"


		      // protocol and subdomain match
		      "<tr>"
		      "<td>http://www.goodstuff.com/</td>"
		      "<td>"
		      "Matches urls beginning with "
		      "http://www.goodstuff.com/. "
		      "This is also used as a seed."
		      "</td>"
		      "</tr>"

		      // subdomain match
		      "<tr>"
		      "<td>www.goodstuff.com</td>"
		      "<td>"
		      "Exact subdomain match. "
		      "Spider urls on www.goodstuff.com."
		      "This is also used as a seed."
		      "</td>"
		      "</tr>"

		      // domain match
		      "<tr>"
		      "<td>goodstuff.com</td>"
		      "<td>"
		      "Exact domain match. "
		      "Spider urls on goodstuff.com and on "
		      "any subdomain of goodstuff.com."
		      "This is also used as a seed, but you might "
		      "have to end up adding www.goodstuff.com separately."
		      "</td>"
		      "</tr>"

		      // subdomain AND subdir match
		      "<tr>"
		      "<td>www.goodstuff.com/goodir/anotherdir/</td>"
		      "<td>"
		      "Exact subdomain match and parital subdir match. "
		      "Matches urls on www.goodstuff.com and in the "
		      "/gooddir/anotherdir/ subdirectory. "
		      "This is also used as a seed, but you might "
		      "have to end up adding another seed if it is 404."
		      "</td>"
		      "</tr>"


		      // exact match
		      "<tr>"
		      "<td>exact:http://xyz.goodstuff.com/</td>"
		      "<td>"
		      "Seed url. "
		      "Matches the single url "
		      "http://xyz.goodstuff.com/."
		      "This is also used as a seed."
		      "</td>"
		      "</tr>"

		      // exact match
		      "<tr>"
		      "<td>exact:http://xyz.goodstuff.com/mypage.html</td>"
		      "<td>"
		      "Matches the single url "
		      "http://xyz.goodstuff.com/mypage.html."
		      "This is also used as a seed."
		      "</td>"
		      "</tr>"

		      // local subdir match
		      "<tr>"
		      "<td>file://C/mydir/mysubdir/"
		      "<td>"
		      "Matches all local files in the specified directory. "
		      "This is also used as a seed."
		      "</td>"
		      "</tr>"

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
		      "<td>-badstuff.com</td>"
		      "<td>Exclude all pages from the badstuff.com domain. "
		      "Start the url pattern with a - to exclude it from "
		      "the spider set.</td>"
		      "</tr>"

		      "<tr>"
		      "<td>regexp:-pid=[0-9A-Z]+/</td>"
		      "<td>Url must match this regular expression. "
		      "Try to avoid using these if possible; they can slow "
		      "things down and are confusing to use."
		      "</td>"
		      "</tr>"
		      

		      // tag match
		      "<tr><td>"
		      //"<td>tag:boots contains:boots<br>"
		      "tag:boots www.westernfootwear.com<br>"
		      "tag:boots www.cowboyshop.com<br>"
		      "tag:boots www.moreboots.com<br>"
		      "tag:boots www.lotsoffootwear.com<br>"
		      //"<td>t:boots -contains:www.cowboyshop.com/shoes/</td>"
		      "</td><td>"
		      "Advance users only. "
		      "Tag any urls matching these 4 url patterns "
		      "so we can use "
		      "the expression <i>tag:boots</i> in the "
		      "<a href=/scheduler>spider scheduler</a> and perhaps "
		      "give such urls higher spider priority."
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


// from pagecrawlbot.cpp for printCrawlDetailsInJson()
#include "PageCrawlBot.h"

///////////
//
// main > Basic > Status
//
///////////
bool sendPageBasicStatus ( TcpSocket *socket , HttpRequest *hr ) {

	char  buf [ 128000 ];
	SafeBuf sb(buf,128000);

	char *fs = hr->getString("format",NULL,NULL);
	char fmt = FORMAT_HTML;
	if ( fs && strcmp(fs,"html") == 0 ) fmt = FORMAT_HTML;
	if ( fs && strcmp(fs,"json") == 0 ) fmt = FORMAT_JSON;
	if ( fs && strcmp(fs,"xml") == 0 ) fmt = FORMAT_XML;


	// true = usedefault coll?
	CollectionRec *cr = g_collectiondb.getRec ( hr , true );
	if ( ! cr ) {
		g_httpServer.sendErrorReply(socket,500,"invalid collection");
		return true;
	}

	if ( fmt == FMT_JSON ) {
		printCrawlDetailsInJson ( &sb , cr );
		return g_httpServer.sendDynamicPage (socket, 
						     sb.getBufStart(), 
						     sb.length(),
						     0); // cachetime
	}


	// print standard header 
	if ( fmt == FORMAT_HTML )
		// this prints the <form tag as well
		g_pages.printAdminTop ( &sb , socket , hr );


	//
	// show stats
	//
	if ( fmt == FMT_HTML ) {

		char *seedStr = cr->m_diffbotSeeds.getBufStart();
		if ( ! seedStr ) seedStr = "";

		SafeBuf tmp;
		long crawlStatus = -1;
		getSpiderStatusMsg ( cr , &tmp , &crawlStatus );
		CrawlInfo *ci = &cr->m_localCrawlInfo;
		long sentAlert = (long)ci->m_sentCrawlDoneAlert;
		if ( sentAlert ) sentAlert = 1;

		sb.safePrintf(

			      "<form method=get action=/crawlbot>"
			      "%s"
			      , sb.getBufStart() // hidden input token/name/..
			      );
		sb.safePrintf("<TABLE border=0>"
			      "<TR><TD valign=top>"

			      "<table border=0 cellpadding=5>"

			      //
			      "<tr>"
			      "<td><b>Crawl Name:</td>"
			      "<td>%s</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Crawl Type:</td>"
			      "<td>%li</td>"
			      "</tr>"

			      //"<tr>"
			      //"<td><b>Collection Alias:</td>"
			      //"<td>%s%s</td>"
			      //"</tr>"

			      "<tr>"
			      "<td><b>Token:</td>"
			      "<td>%s</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Seeds:</td>"
			      "<td>%s</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Crawl Status:</td>"
			      "<td>%li</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Crawl Status Msg:</td>"
			      "<td>%s</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Rounds Completed:</td>"
			      "<td>%li</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Has Urls Ready to Spider:</td>"
			      "<td>%li</td>"
			      "</tr>"


			      // this will  have to be in crawlinfo too!
			      //"<tr>"
			      //"<td><b>pages indexed</b>"
			      //"<td>%lli</td>"
			      //"</tr>"

			      "<tr>"
			      "<td><b>Objects Found</b></td>"
			      "<td>%lli</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>URLs Harvested</b> (inc. dups)</td>"
			      "<td>%lli</td>"
     
			      "</tr>"

			      //"<tr>"
			      //"<td><b>URLs Examined</b></td>"
			      //"<td>%lli</td>"
			      //"</tr>"

			      "<tr>"
			      "<td><b>Page Crawl Attempts</b></td>"
			      "<td>%lli</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Page Crawl Successes</b></td>"
			      "<td>%lli</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Page Crawl Successes This Round</b></td>"
			      "<td>%lli</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Page Process Attempts</b></td>"
			      "<td>%lli</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Page Process Successes</b></td>"
			      "<td>%lli</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Page Process Successes This Round</b></td>"
			      "<td>%lli</td>"
			      "</tr>"

			      
			      , cr->m_diffbotCrawlName.getBufStart()
			      
			      , (long)cr->m_isCustomCrawl

			      , cr->m_diffbotToken.getBufStart()

			      , seedStr

			      , crawlStatus
			      , tmp.getBufStart()
			      , cr->m_spiderRoundNum
			      , cr->m_globalCrawlInfo.m_hasUrlsReadyToSpider

			      , cr->m_globalCrawlInfo.m_objectsAdded -
			        cr->m_globalCrawlInfo.m_objectsDeleted
			      , cr->m_globalCrawlInfo.m_urlsHarvested
			      //, cr->m_globalCrawlInfo.m_urlsConsidered

			      , cr->m_globalCrawlInfo.m_pageDownloadAttempts
			      , cr->m_globalCrawlInfo.m_pageDownloadSuccesses
			      , cr->m_globalCrawlInfo.m_pageDownloadSuccessesThisRound

			      , cr->m_globalCrawlInfo.m_pageProcessAttempts
			      , cr->m_globalCrawlInfo.m_pageProcessSuccesses
			      , cr->m_globalCrawlInfo.m_pageProcessSuccessesThisRound
			      );

	}

	if ( fmt != FORMAT_JSON )
		// wrap up the form, print a submit button
		g_pages.printAdminBottom ( &sb );

	return g_httpServer.sendDynamicPage (socket, 
					     sb.getBufStart(), 
					     sb.length(),
					     0); // cachetime
}
	
