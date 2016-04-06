#include "gb-include.h"

#include "Indexdb.h"     // makeKey(int64_t docId)
#include "Titledb.h"
#include "Spider.h"
#include "Tagdb.h"
#include "Dns.h"
//#include "PageResults.h" // for query buf, g_qbuf
#include "Collectiondb.h"
//#include "CollectionRec.h"
#include "Clusterdb.h"    // for getting # of docs indexed
//#include "Checksumdb.h"   // should migrate to this one, though
#include "Pages.h"
#include "Query.h"        // MAX_QUERY_LEN
#include "SafeBuf.h"
#include "LanguageIdentifier.h"
#include "LanguagePages.h"
#include "Users.h"
#include "Address.h" // getIPLocation
#include "Proxy.h"

//char *printNumResultsDropDown ( char *p, int32_t n, bool *printedDropDown);
bool printNumResultsDropDown ( SafeBuf& sb, int32_t n, bool *printedDropDown);
//static char *printTopDirectory ( char *p, char *pend );
static bool printTopDirectory ( SafeBuf& sb , char format );

// this prints the last five queries
//static int32_t printLastQueries ( char *p , char *pend ) ;

//static char *expandRootHtml ( char *p    , int32_t plen    ,
/*
static bool expandRootHtml  ( SafeBuf& sb,
			      uint8_t *html , int32_t htmlLen ,
			      char *q    , int32_t qlen    ,
			      HttpRequest *r ,
			      TcpSocket   *s ,
			      int64_t docsInColl ,
			      CollectionRec *cr ) ;
*/

bool sendPageRoot ( TcpSocket *s, HttpRequest *r ){
	return sendPageRoot ( s, r, NULL );
}

bool printNav ( SafeBuf &sb , HttpRequest *r ) {

	/*
	char *root       = "";
	char *rootSecure = "";
	if ( g_conf.m_isMattWells ) {
		root       = "http://www.gigablast.com";
		rootSecure = "https://www.gigablast.com";
	}

	sb.safePrintf("<center><b><p class=nav>"
		      "<a href=%s/about.html>About</a>"
		      " &nbsp; &nbsp; "
		      "<a href=%s/contact.html>Contact</a>"
		      " &nbsp; &nbsp; "
		      "<a href=%s/help.html>Help</a>"
		      " &nbsp; &nbsp; "
		      "<a href=%s/privacy.html>Privacy Policy</a>"
		      " &nbsp; &nbsp; "

		      // TODO: API page must also provide a description
		      // of the output... like searchfeed.html does already.
		      // put that in the api page as well.
		      "<a href=%s/api>API</a>"
		      , root
		      , root
		      , root
		      , root
		      , root
		      );

	if ( g_conf.m_isMattWells )
		sb.safePrintf(" &nbsp; &nbsp; "
			      "<a href=%s/seoapi.html>SEO API</a>"
			      " &nbsp; &nbsp; "
			      "<a href=%s/account>My Account</a> "
			      , root
			      , rootSecure
			      //" &nbsp; &nbsp; <a href=/logout>Logout</a>"
			      );

	//if ( r->isLocal() )
	    sb.safePrintf("&nbsp; &nbsp; &nbsp; [<a style=color:green; "
			  "href=\"/admin/settings\">"
			  "Admin</a>]");
	    sb.safePrintf("</p></b></center>");
	*/

	sb.safePrintf("</TD></TR></TABLE>"
		      "</body></html>");
	return true;
}

//////////////
//
// BEGIN expandHtml() helper functions
//
//////////////

bool printFamilyFilter ( SafeBuf& sb , bool familyFilterOn ) {
	char *s1 = "";
	char *s2 = "";
	if ( familyFilterOn ) s1 = " checked";
	else                  s2 = " checked";
	//p += sprintf ( p ,
	return sb.safePrintf (
		       "Family filter: "
		       "<input type=radio name=ff value=1%s>On &nbsp; "
		       "<input type=radio name=ff value=0%s>Off &nbsp; " ,
		       s1 , s2 );
	//return p;
}

//char *printNumResultsDropDown ( char *p , int32_t n , bool *printedDropDown ) {
bool printNumResultsDropDown ( SafeBuf& sb , int32_t n , bool *printedDropDown ) {
	if ( n!=10 && n!=20 && n!=30 && n!=50 && n!=100 )
		//return p;
		return true;
	*printedDropDown = true;
	char *d1 = "";
	char *d2 = "";
	char *d3 = "";
	char *d4 = "";
	char *d5 = "";
	if ( n == 10 ) d1 = " selected";
	if ( n == 20 ) d2 = " selected";
	if ( n == 30 ) d3 = " selected";
	if ( n == 50 ) d4 = " selected";
	if ( n ==100 ) d5 = " selected";
	//p += sprintf ( p , 
	return sb.safePrintf (
		       "<select name=n>\n"
		       "<option value=10%s>10\n"
		       "<option value=20%s>20\n"
		       "<option value=30%s>30\n"
		       "<option value=50%s>50\n"
		       "<option value=100%s>100\n"
		       "</select>",
		       d1,d2,d3,d4,d5);
	//return p;
}

//char *printDirectorySearchType ( char *p, int32_t sdirt ) {
bool printDirectorySearchType ( SafeBuf& sb, int32_t sdirt ) {
	// default to entire directory
	if (sdirt < 1 || sdirt > 4)
		sdirt = 3;

	// by default search the whole thing
	sb.safePrintf("<input type=\"radio\" name=\"sdirt\" value=\"3\"");
	if (sdirt == 3) sb.safePrintf(" checked>");
	else            sb.safePrintf(">");
	sb.safePrintf("Entire Directory<br>\n");
	// entire category
	sb.safePrintf("<input type=\"radio\" name=\"sdirt\" value=\"1\"");
	if (sdirt == 1) sb.safePrintf(" checked>");
	else            sb.safePrintf(">");
	sb.safePrintf("Entire Category<br>\n");
	// base category only
	sb.safePrintf("<nobr><input type=\"radio\" name=\"sdirt\" value=\"2\"");
	if (sdirt == 2) sb.safePrintf(" checked>");
	else            sb.safePrintf(">"); 
	sb.safePrintf("Pages in Base Category</nobr><br>\n");
	// sites in base category
	sb.safePrintf("<input type=\"radio\" name=\"sdirt\" value=\"7\"");
	if (sdirt == 7) sb.safePrintf(" checked>");
	else            sb.safePrintf(">");
	sb.safePrintf("Sites in Base Category<br>\n");
	// sites in entire category
	sb.safePrintf("<input type=\"radio\" name=\"sdirt\" value=\"6\"");
	if (sdirt == 6) sb.safePrintf(" checked>");
	else            sb.safePrintf(">");
	sb.safePrintf("Sites in Entire Category<br>\n");
	// end it
	return true;
}


#include "SearchInput.h"

bool printRadioButtons ( SafeBuf& sb , SearchInput *si ) {
	// don't display this for directory search
	// look it up. returns catId <= 0 if dmoz not setup yet.
	// From PageDirectory.cpp
	//int32_t catId= g_categories->getIdFromPath(decodedPath, decodedPathLen);
	// if /Top print the directory homepage
	//if ( catId == 1 || catId <= 0 ) 
	//	return true;

	// site
	/*
	if ( si->m_siteLen > 0 ) {
		// . print rest of search box etc.
		// . print cobranding radio buttons
		//if ( p + si->m_siteLen + 1 >= pend ) return p;
		//p += sprintf ( p , 
		return sb.safePrintf (
			  //" &nbsp; "
			  //"<font size=-1>"
			  //"<b><a href=\"/\"><font color=red>"
			  //"Powered by Gigablast</font></a></b>"
			  //"<br>"
			  //"<tr align=center><td></td><td>"
			  "<input type=radio name=site value=\"\">"
			  "Search the Web "
			  "<input type=radio name=site "
			  "value=\"%s\"  checked>Search %s" ,
			  //"</td></tr></table><br>"
			  //"</td></tr>"
			  //"<font size=-1>" ,
			  si->m_site , si->m_site );
	}
	else if ( si->m_sitesLen > 0 ) {
	*/
	if ( si->m_sites && si->m_sites[0] ) {
		// . print rest of search box etc.
		// . print cobranding radio buttons
		//if ( p + si->m_sitesLen + 1 >= pend ) return p;
		// if not explicitly instructed to print all sites
		// and they are a int32_t list, do not print all
		/*
		char tmp[1000];
		char *x = si->m_sites;
		if ( si->m_sitesLen > 255){//&&!st->m_printAllSites){
			// copy what's there
			strncpy ( tmp , si->m_sites , 255 );
			x = tmp + 254 ;
			// do not hack off in the middle of a site
			while ( is_alnum(*x) && x > tmp ) x--;
			// overwrite it with [more] link
			//x += sprintf ( x , "<a href=\"/search?" );
			// our current query parameters
			//if ( x + uclen + 10 >= xend ) goto skipit;
			sprintf ( x , " ..." );
			x = tmp;
		}
		*/
		//p += sprintf ( p , 
		sb.safePrintf (
			  //" &nbsp; "
			  //"<font size=-1>"
			  //"<b><a href=\"/\"><font color=red>"
			  //"Powered by Gigablast</font></a></b>"
			  //"<br>"
			  //"<tr align=center><td></td><td>"
			  "<input type=radio name=sites value=\"\">"
			  "Search the Web "
			  "<input type=radio name=sites "
			  "value=\"%s\"  checked>Search ",
			  //"</td></tr></table><br>"
			  //"</td></tr>"
			  //"<font size=-1>" ,
			  si->m_sites );
		sb.safeTruncateEllipsis ( si->m_sites, 255 );
	}
	return true;
}

bool printLogo ( SafeBuf& sb , SearchInput *si ) {
	// if an image was provided...
	if ( ! si->m_imgUrl || ! si->m_imgUrl[0] ) {
		// no, now we default to our logo
		//return true;
		//p += sprintf ( p ,
		return sb.safePrintf (
			  "<a href=\"/\">"
			  "<img valign=top width=250 height=61 border=0 "
			  // avoid https for this, so make it absolute
			  "src=\"/logo-med.jpg\"></a>" );
		//return p;
	}
	// do we have a link?
	if ( si->m_imgLink && si->m_imgLink[0])
		//p += sprintf ( p , "<a href=\"%s\">",si->m_imgLink);
		sb.safePrintf ( "<a href=\"%s\">", si->m_imgLink );
	// print image width and length
	if ( si->m_imgWidth >= 0 && si->m_imgHeight >= 0 ) 
		//p += sprintf ( p , "<img width=%"INT32" height=%"INT32" ",
		sb.safePrintf( "<img width=%"INT32" height=%"INT32" ",
			       si->m_imgWidth , si->m_imgHeight );
	else
		//p += sprintf ( p , "<img " );
		sb.safePrintf ( "<img " );

	//p += sprintf ( p , "border=0 src=\"%s\">",
	sb.safePrintf( "border=0 src=\"%s\">",
		       si->m_imgUrl );
	// end the link if we had one
	if ( si->m_imgLink && si->m_imgLink[0] ) 
		//p += sprintf ( p , "</a>");
		sb.safePrintf ( "</a>");

	return true;
}

/////////////
//
// END expandHtml() helper functions
//
/////////////


bool expandHtml (  SafeBuf& sb,
		   char *head , 
		   int32_t hlen ,
		   char *q    , 
		   int32_t qlen ,
		   HttpRequest *r ,
		   SearchInput *si,
		   char *method ,
		   CollectionRec *cr ) {
	//char *pend = p + plen;
	// store custom header into buf now
	//for ( int32_t i = 0 ; i < hlen && p+10 < pend ; i++ ) {
	for ( int32_t i = 0 ; i < hlen; i++ ) {
		if ( head[i] != '%'   ) {
			// *p++ = head[i];
			sb.safeMemcpy((char*)&head[i], 1);
			continue;
		}
		if ( i + 1 >= hlen    ) {
			// *p++ = head[i];
			sb.safeMemcpy((char*)&head[i], 1);
			continue;
		}
		if ( head[i+1] == 'S' ) { 
			// now we got the %S, insert "spiders are [on/off]"
			bool spidersOn = true;
			if ( ! g_conf.m_spideringEnabled ) spidersOn = false;
			if ( ! cr->m_spideringEnabled ) spidersOn = false;
			if ( spidersOn ) 
				sb.safePrintf("Spiders are on");
			else
				sb.safePrintf("Spiders are off");
			// skip over %S
			i += 1;
			continue;
		}

		if ( head[i+1] == 'q' ) { 
			// now we got the %q, insert the query
			char *p    = (char*) sb.getBuf();
			char *pend = (char*) sb.getBufEnd();
			int32_t eqlen = dequote ( p , pend , q , qlen );
			//p += eqlen;
			sb.incrementLength(eqlen);
			// skip over %q
			i += 1;
			continue;
		}

		if ( head[i+1] == 'c' ) { 
			// now we got the %q, insert the query
			if ( cr ) sb.safeStrcpy(cr->m_coll);
			// skip over %c
			i += 1;
			continue;
		}

		if ( head[i+1] == 'w' &&
		     head[i+2] == 'h' &&
		     head[i+3] == 'e' &&
		     head[i+4] == 'r' &&
		     head[i+5] == 'e' ) {
			// insert the location
			int32_t whereLen;
			char *where = r->getString("where",&whereLen);
			// get it from cookie as well!
			if ( ! where ) 
				where = r->getStringFromCookie("where",
							       &whereLen);
			// fix for getStringFromCookie
			if ( where && ! where[0] ) where = NULL;
			// skip over the %where
			i += 5;
			// if empty, base it on IP
			if ( ! where ) {
				double lat;
				double lon;
				double radius;
				char *city,*state,*ctry;
				// use this by default
				int32_t ip = r->m_userIP;
				// ip for testing?
				int32_t iplen;
				char *ips = r->getString("uip",&iplen);
				if ( ips ) ip = atoip(ips);
				// returns true if found in db
				char buf[128];
				getIPLocation ( ip ,
						&lat , 
						&lon , 
						&radius,
						&city ,
						&state ,
						&ctry  ,
						buf    ,
						128    ) ;
				if ( city && state )
					sb.safePrintf("%s, %s",city,state);
			}
			else
				sb.dequote (where,whereLen);
			continue;
		}
		if ( head[i+1] == 'w' &&
		     head[i+2] == 'h' &&
		     head[i+3] == 'e' &&
		     head[i+4] == 'n' ) {
			// insert the location
			int32_t whenLen;
			char *when = r->getString("when",&whenLen);
			// skip over the %when
			i += 4;
			if ( ! when ) continue;
			sb.dequote (when,whenLen);
			continue;
		}
		// %sortby
		if ( head[i+1] == 's' &&
		     head[i+2] == 'o' &&
		     head[i+3] == 'r' &&
		     head[i+4] == 't' &&
		     head[i+5] == 'b' &&
		     head[i+6] == 'y' ) {
			// insert the location
			int32_t sortBy = r->getLong("sortby",1);
			// print the radio buttons
			char *cs[5];
			cs[0]="";
			cs[1]="";
			cs[2]="";
			cs[3]="";
			cs[4]="";
			if ( sortBy >=1 && sortBy <=4 )
				cs[sortBy] = " checked";
			sb.safePrintf(
			 "<input type=radio name=sortby value=1%s>date "
			 "<input type=radio name=sortby value=2%s>distance "
			 "<input type=radio name=sortby value=3%s>relevancy "
			 "<input type=radio name=sortby value=4%s>popularity",
			 cs[1],cs[2],cs[3],cs[4]);
			// skip over the %sortby
			i += 6;
			continue;
		}
		if ( head[i+1] == 'e' ) { 
			// now we got the %e, insert the query
			char *p    = (char*) sb.getBuf();
			int32_t  plen = sb.getAvail();
			int32_t eqlen = urlEncode ( p , plen , q , qlen );
			//p += eqlen;
			sb.incrementLength(eqlen);
			// skip over %e
			i += 1;
			continue;
		}
		if ( head[i+1] == 'N' ) { 
			// now we got the %N, insert the global doc count
			//int64_t c=g_checksumdb.getRdb()->getNumGlobalRecs();
			//now each host tells us how many docs it has in itsping
			int64_t c = g_hostdb.getNumGlobalRecs();
			c += g_conf.m_docCountAdjustment;
			// never allow to go negative
			if ( c < 0 ) c = 0;
			//p+=ulltoa(p,c);
			char *p = (char*) sb.getBuf();
			sb.reserve2x(16);
			int32_t len = ulltoa(p, c);
			sb.incrementLength(len);
			// skip over %N
			i += 1;
			continue;
		}
		/*
		if ( head[i+1] == 'E' ) { 
			// now each host tells us how many docs it has in its
			// ping request
			int64_t c = g_hostdb.getNumGlobalEvents();
			char *p = (char*) sb.getBuf();
			sb.reserve2x(16);
			int32_t len = ulltoa(p, c);
			sb.incrementLength(len);
			// skip over %E
			i += 1;
			continue;
		}
		*/
		if ( head[i+1] == 'n' ) { 
			// now we got the %n, insert the collection doc count
			//p+=ulltoa(p,docsInColl);
			char *p = (char*) sb.getBuf();
			sb.reserve2x(16);
			int64_t docsInColl = 0;
			if ( cr ) docsInColl = cr->getNumDocsIndexed();
			int32_t len = ulltoa(p, docsInColl);
			sb.incrementLength(len);
			// skip over %n
			i += 1;
			continue;
		}
		/*
		if ( head[i+1] == 'T' ) { 
			// . print the final tail
			// . only print admin link if we're local
			//int32_t  user = g_pages.getUserType ( s , r );
			//char *username = g_users.getUsername(r);
			//char *pwd  = r->getString ( "pwd" );
			char *p    = (char*) sb.getBuf();
			int32_t  plen = sb.getAvail();
			//p = g_pages.printTail ( p , p + plen , user , pwd );
			char *n = g_pages.printTail(p , p + plen ,
						    r->isLocal());
			sb.incrementLength(n - p);
			// skip over %T
			i += 1;
			continue;
		}
		*/
		// print the drop down menu for selecting the # of reslts
		if ( head[i+1] == 'D' ) {
			// skip over %D
			i += 1;
			// skip if not enough buffer
			//if ( p + 1000 >= pend ) continue; 
			// # results
			//int32_t n = r->getLong("n",10);
			//bool printedDropDown;
			//p = printNumResultsDropDown(p,n,&printedDropDown);
			//printNumResultsDropDown(sb,n,&printedDropDown);
			continue;
		}
		if ( head[i+1] == 'H' ) { 
			// . insert the secret key here, to stop seo bots
			// . TODO: randomize its position to make parsing more 
			//         difficult
			// . this secret key is for submitting a new query
			// int32_t key;
			// char kname[4];
			// g_httpServer.getKey (&key,kname,NULL,0,time(NULL),0,
			// 		     10);
			//sprintf (p , "<input type=hidden name=%s value=%"INT32">",
			//	  kname,key);
			//p += gbstrlen ( p );
			// sb.safePrintf( "<input type=hidden name=%s "
			//"value=%"INT32">",
			// 	       kname,key);

			//adds param for default screen size
			//if(cr)
			//	sb.safePrintf("<input type=hidden "
			//"id='screenWidth' name='ws' value=%"INT32">", 
			//cr->m_screenWidth);

			// insert collection name too
			int32_t collLen;
			char *coll = r->getString ( "c" , &collLen );
			if ( collLen > 0 && collLen < MAX_COLL_LEN ) {
			        //sprintf (p,"<input type=hidden name=c "
				//	 "value=\"");
				//p += gbstrlen ( p );	
				sb.safePrintf("<input type=hidden name=c "
					      "value=\"");
				//gbmemcpy ( p , coll , collLen );
				//p += collLen;
				sb.safeMemcpy(coll, collLen);
				//sprintf ( p , "\">\n");
				//p += gbstrlen ( p );	
				sb.safePrintf("\">\n");
			}

			// pass this crap on so zak can do searches
			//char *username = g_users.getUsername(r);
			// this is null because not in the cookie and we are
			// logged in
			//char *pwd  = r->getString ( "pwd" );
			//sb.safePrintf("<input type=hidden name=pwd "
			//"value=\"%s\">\n",
			//pwd);
			//sb.safePrintf("<input type=hidden name=username "
			//	      "value=\"%s\">\n",username);

			// skip over %H
			i += 1;
			continue;
		}
		// %t, print Top Directory section
		if ( head[i+1] == 't' ) {
			i += 1;
			//p = printTopDirectory ( p, pend );
			printTopDirectory ( sb , FORMAT_HTML );
			continue;
		}

		// MDW

		if ( head[i+1] == 'F' ) {
			i += 1;
			//p = printTopDirectory ( p, pend );
			if ( ! method ) method = "GET";
			sb.safePrintf("<form method=%s action=\"/search\" "
				      "name=\"f\">\n",method);
			continue;
		}

		if ( head[i+1] == 'L' ) {
			i += 1;
			//p = printTopDirectory ( p, pend );
			printLogo ( sb , si );
			continue;
		}

		if ( head[i+1] == 'f' ) {
			i += 1;
			//p = printTopDirectory ( p, pend );
			printFamilyFilter ( sb , si->m_familyFilter );
			continue;
		}

		if ( head[i+1] == 'R' ) {
			i += 1;
			//p = printTopDirectory ( p, pend );
			printRadioButtons ( sb , si );
			continue;
		}

		// MDW

		// *p++ = head[i];
		sb.safeMemcpy((char*)&head[i], 1);
		continue;
	}
	//return p;
	return true;
}


bool printLeftColumnRocketAndTabs ( SafeBuf *sb , 
				    bool isSearchResultsPage ,
				    CollectionRec *cr ,
				    char *tabName ) {

	class MenuItem {
	public:
		char *m_text;
		char *m_url;
	};

	static MenuItem mi[] = {

		{"SEARCH","/"},

 		// {"DISCUSSIONS","/?searchtype=discussions"},
 		// {"PRODUCTS","/?searchtype=products"},
 		// {"ARTICLES","/?searchtype=articles"},
 		// {"IMAGES","/?searchtype=images"},

		{"DIRECTORY","/Top"},
		{"ADVANCED","/adv.html"},
		{"ADD URL","/addurl"},
		{"WIDGETS","/widgets.html"},
		{"SYNTAX","/syntax.html"},
		{"USERS","/users.html"},
		{"ABOUT","/about.html"},
		{"BLOG","/blog.html"},
		// take this out for now
		//{"FEED","/searchfeed.html"},
		{"FAQ","/faq.html"},
		{"API","/api.html"}
	};

	char *coll = "";
	if ( cr ) coll = cr->m_coll;

	//
	// first the nav column
	//
	sb->safePrintf(
		       "<TD bgcolor=#%s " // f3c714 " // yellow/gold
		      "valign=top "
		      "style=\"width:210px;"
		      "border-right:3px solid blue;"
		      "\">"

		      "<br>"

		      "<center>"
		      "<a href=/?c=%s>"
		      "<div style=\""
		      "background-color:white;"
		      "padding:10px;"
		      "border-radius:100px;"
		      "border-color:blue;"
		      "border-width:3px;"
		      "border-style:solid;"
		      "width:100px;"
		      "height:100px;"
		      "\">"
		       , GOLD
		       , coll
		       );

	if ( strcmp(tabName,"appliance") == 0 )
		sb->safePrintf("<img style=margin-top:21px; width=90 "
			       "height=57 src=/computer2.png>");
	else
		sb->safePrintf("<br style=line-height:10px;>"
			       "<img border=0 "
			       "width=54 height=79 src=/rocket.jpg>"
			       );

	sb->safePrintf ( "</div>"
			 "</a>"
			 "</center>"

			 "<br>"
			 "<br>"
		      );

	int32_t n = sizeof(mi) / sizeof(MenuItem);


	for ( int32_t i = 0 ; i < n ; i++ ) {

		// just show search, directort and advanced tab in serps
		if ( isSearchResultsPage && i >= 3 ) break;

		// what was this for?
		// if ( i >= 1 && i <= 4 &&
		//      cr->m_diffbotApiUrl.length() >= 0 )
		// 	continue;

		char delim = '?';
		if ( strstr ( mi[i].m_url,"?") ) delim = '&';

		sb->safePrintf(
			      "<a href=%s%cc=%s>"
			      "<div style=\""
			      "padding:5px;"
			      "position:relative;"
			      "text-align:right;"
			      "border-width:3px;"
			      "border-right-width:0px;"
			      "border-style:solid;"
			      "margin-left:10px;"
			      "border-top-left-radius:10px;"
			      "border-bottom-left-radius:10px;"
			      "font-size:14px;"
			      "x-overflow:;"
			      , mi[i].m_url
			      , delim
			      , coll
			      );
		//if ( i == pageNum )
		bool matched = false;
		if ( strcasecmp(mi[i].m_text,tabName) == 0 )
			matched = true;

		if ( matched )
			sb->safePrintf(
				      "border-color:blue;"
				      "color:black;"
				      "background-color:white;\" ");
		else
			sb->safePrintf("border-color:white;"
				      "color:white;"
				      "background-color:blue;\" "
				      " onmouseover=\""
				      "this.style.backgroundColor='lightblue';"
				      "this.style.color='black';\""
				      " onmouseout=\""
				      "this.style.backgroundColor='blue';"
				      "this.style.color='white';\""
				      );

		sb->safePrintf(">"
			      // make button wider
			      "<nobr>"
			      "&nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; "
			      "<b>%s</b> &nbsp; &nbsp;</nobr>"
			      , mi[i].m_text
			      );
		//
		// begin hack: white out the blue border line!!
		//
		if ( matched )
			sb->safePrintf(
				      "<div style=padding:5px;top:0;"
				      "background-color:white;"
				      "display:inline-block;"
				      "position:absolute;>"
				      "&nbsp;"
				      "</div>"
				      );
		// end hack
		sb->safePrintf(
			      "</div>"
			      "</a>"
			      "<br>"
			      );
	}



	// admin link
	if ( isSearchResultsPage ) return true;

	sb->safePrintf(
		      "<a href=/admin/settings?c=%s>"
		      "<div style=\"background-color:green;"
		      // for try it out bubble:
		      //"position:relative;"
		      "padding:5px;"
		      "text-align:right;"
		      "border-width:3px;"
		      "border-right-width:0px;"
		      "border-style:solid;"
		      "margin-left:10px;"
		      "border-color:white;"
		      "border-top-left-radius:10px;"
		      "border-bottom-left-radius:10px;"
		      "font-size:14px;"
		      "color:white;"
		      "cursor:hand;"
		      "cursor:pointer;\" "
		      " onmouseover=\""
		      "this.style.backgroundColor='lightgreen';"
		      "this.style.color='black';\""
		      " onmouseout=\""
		      "this.style.backgroundColor='green';"
		      "this.style.color='white';\""
		      ">"

		      /*
		      // try it out bubble div
		      "<div "

		      " onmouseover=\""
		      "this.style.box-shadow='10px 10px 5px #888888';"
		      "\""
		      " onmouseout=\""
		      "this.style.box-shadow='';"
		      "\""

		      "style=\""
		      "vertical-align:middle;"
		      "text-align:left;"
		      "cursor:pointer;"
		      "cursor:hand;"
		      //"border-color:black;"
		      //"border-style:solid;"
		      //"border-width:2px;"
		      "padding:3px;"
		      //"width:30px;"
		      //"height:20px;"
		      //"margin-top:-20px;"
		      "margin-left:-120px;"
		      "position:absolute;"
		      //"top:-20px;"
		      //"left:10px;"
		      "display:inline-block;"
		      "\""
		      ">"
		      "<b style=font-size:11px;>"
		      "Click for demo"
		      "</b>"
		      "</div>"
		      */
		      // end try it out bubble div




		      "<b>ADMIN</b> &nbsp; &nbsp;"
		      "</div>"
		      "</a>"
		      "<br>"

		      "</TD>"
		      , coll
		      );

	return true;
}

bool printFrontPageShell ( SafeBuf *sb , char *tabName , CollectionRec *cr ,
			   bool printGigablast ) {

	sb->safePrintf("<html>\n");
	sb->safePrintf("<head>\n");
	//sb->safePrintf("<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf8\">");
	sb->safePrintf("<meta name=\"description\" content=\"A powerful, new search engine that does real-time indexing!\">\n");
	sb->safePrintf("<meta name=\"keywords\" content=\"search, search engine, search engines, search the web, fresh index, green search engine, green search, clean search engine, clean search\">\n");
	//char *title = "An Alternative Open Source Search Engine";
	char *title = "An Alternative Open Source Search Engine";
	if ( strcasecmp(tabName,"search") ) title = tabName;
	// if ( pageNum == 1 ) title = "Directory";
	// if ( pageNum == 2 ) title = "Advanced";
	// if ( pageNum == 3 ) title = "Add Url";
	// if ( pageNum == 4 ) title = "About";
	// if ( pageNum == 5 ) title = "Help";
	// if ( pageNum == 6 ) title = "API";
	sb->safePrintf("<title>Gigablast - %s</title>\n",title);
	sb->safePrintf("<style><!--\n");
	sb->safePrintf("body {\n");
	sb->safePrintf("font-family:Arial, Helvetica, sans-serif;\n");
	sb->safePrintf("color: #000000;\n");
	sb->safePrintf("font-size: 12px;\n");
	sb->safePrintf("margin: 0px 0px;\n");
	sb->safePrintf("letter-spacing: 0.04em;\n");
	sb->safePrintf("}\n");
	sb->safePrintf("a {text-decoration:none;}\n");
	//sb->safePrintf("a:link {color:#00c}\n");
	//sb->safePrintf("a:visited {color:#551a8b}\n");
	//sb->safePrintf("a:active {color:#f00}\n");
	sb->safePrintf(".bold {font-weight: bold;}\n");
	sb->safePrintf(".bluetable {background:#d1e1ff;margin-bottom:15px;font-size:12px;}\n");
	sb->safePrintf(".url {color:#008000;}\n");
	sb->safePrintf(".cached, .cached a {font-size: 10px;color: #666666;\n");
	sb->safePrintf("}\n");
	sb->safePrintf("table {\n");
	sb->safePrintf("font-family:Arial, Helvetica, sans-serif;\n");
	sb->safePrintf("color: #000000;\n");
	sb->safePrintf("font-size: 12px;\n");
	sb->safePrintf("}\n");
	sb->safePrintf(".directory {font-size: 16px;}\n"
		      ".nav {font-size:20px;align:right;}\n"
		      );
	sb->safePrintf("-->\n");
	sb->safePrintf("</style>\n");
	sb->safePrintf("\n");
	sb->safePrintf("</head>\n");
	sb->safePrintf("<script>\n");
	sb->safePrintf("<!--\n");
	sb->safePrintf("function x(){document.f.q.focus();}\n");
	sb->safePrintf("// --></script>\n");
	sb->safePrintf("<body onload=\"x()\">\n");
	//sb->safePrintf("<body>\n");
	//g_proxy.insertLoginBarDirective ( &sb );

	//
	// DIVIDE INTO TWO PANES, LEFT COLUMN and MAIN COLUMN
	//


	sb->safePrintf("<TABLE border=0 height=100%% cellspacing=0 "
		      "cellpadding=0>"
		      "\n<TR>\n");


	// . also prints <TD>...</TD>
	// . false = isSearchResultsPage?
	printLeftColumnRocketAndTabs ( sb , false , cr , tabName );


	//
	// now the MAIN column
	//
	sb->safePrintf("\n<TD valign=top style=padding-left:30px;>\n");

	sb->safePrintf("<br><br>");

	if ( ! printGigablast )
		return true;

	sb->safePrintf("<a href=/><img border=0 width=470 "
		      "height=44 src=/gigablast.jpg></a>\n");

	// sb->safePrintf("<br>"
	// 	      "<img border=0 width=470 "
	// 	      "height=15 src=/bar.jpg>\n");

	return true;
}

bool printWebHomePage ( SafeBuf &sb , HttpRequest *r , TcpSocket *sock ) {

	SearchInput si;
	si.set ( sock , r );

	// if there's a ton of sites use the post method otherwise
	// they won't fit into the http request, the browser will reject
	// sending such a large request with "GET"
	char *method = "GET";
	if ( si.m_sites && gbstrlen(si.m_sites)>800 ) method = "POST";

	// if the provided their own
	CollectionRec *cr = g_collectiondb.getRec ( r );
	if ( cr && cr->m_htmlRoot.length() ) {
		return expandHtml (  sb ,
				     cr->m_htmlRoot.getBufStart(),
				     cr->m_htmlRoot.length(),
				     NULL,
				     0,
				     r ,
				     &si,
				     //TcpSocket   *s ,
				     method , // "GET" or "POST"
				     cr );//CollectionRec *cr ) {
	}

	// . search special types
	// . defaults to web which is "search"
	// . can be like "images" "products" "articles"
	char *searchType = r->getString("searchtype",NULL,"search",NULL);
	log("searchtype=%s",searchType);

	// pass searchType in as tabName
	printFrontPageShell ( &sb , searchType , cr , true );


	//sb.safePrintf("<br><br>\n");
	// try to avoid using https for images. it is like 10ms slower.

	// if ( g_conf.m_isMattWells )
	// 	sb.safePrintf("<center><a href=/><img border=0 width=500 "
	// 		      "height=122 src=http://www.gigablast.com/logo-"
	// 		      "med.jpg></a>\n");
	// else


	sb.safePrintf("<br><br>\n");
	sb.safePrintf("<br><br><br>\n");
	/*
	sb.safePrintf("<b>web</b> &nbsp;&nbsp;&nbsp;&nbsp; ");
	if ( g_conf.m_isMattWells )
		sb.safePrintf("<a href=http://www.gigablast.com/seo>seo</a> "
			      "&nbsp;&nbsp;&nbsp;&nbsp; "
			      );
	sb.safePrintf( "<a href=\"/Top\">directory</a> "
		      "&nbsp;&nbsp;&nbsp;&nbsp; \n");
	sb.safePrintf("<a href=/adv.html>advanced search</a>");
	sb.safePrintf(" &nbsp;&nbsp;&nbsp;&nbsp; ");
	sb.safePrintf("<a href=/addurl title=\"Instantly add your url to "
		      "Gigablast's index\">add url</a>");
	sb.safePrintf("\n");
	sb.safePrintf("<br><br>\n");
	*/
	// submit to https now
	sb.safePrintf("<form method=%s "
		      "action=/search name=f>\n", method);

	if ( cr )
		sb.safePrintf("<input type=hidden name=c value=\"%s\">",
			      cr->m_coll);


	// put search box in a box
	sb.safePrintf("<div style="
		      "background-color:#%s;"//fcc714;"
		      "border-style:solid;"
		      "border-width:3px;"
		      "border-color:blue;"
		      //"background-color:blue;"
		      "padding:20px;"
		      "border-radius:20px;"
		      ">"
		      ,GOLD
		      );


	sb.safePrintf("<input name=q type=text "
		      "style=\""
		      //"width:%"INT32"px;"
		      "height:26px;"
		      "padding:0px;"
		      "font-weight:bold;"
		      "padding-left:5px;"
		      //"border-radius:10px;"
		      "margin:0px;"
		      "border:1px inset lightgray;"
		      "background-color:#ffffff;"
		      "font-size:18px;"
		      "\" "

		      "size=40 value=\"\">&nbsp; &nbsp;"

		      //"<input type=\"submit\" value=\"Search\">"

		      "<div onclick=document.f.submit(); "

		      " onmouseover=\""
		      "this.style.backgroundColor='lightgreen';"
		      "this.style.color='black';\""
		      " onmouseout=\""
		      "this.style.backgroundColor='green';"
		      "this.style.color='white';\" "

		      "style=border-radius:28px;"
		      "cursor:pointer;"
		      "cursor:hand;"
		      "border-color:white;"
		      "border-style:solid;"
		      "border-width:3px;"
		      "padding:12px;"
		      "width:20px;"
		      "height:20px;"
		      "display:inline-block;"
		      "background-color:green;color:white;>"
		      "<b style=margin-left:-5px;font-size:18px;"
		      ">GO</b>"
		      "</div>"
		      "\n"
		      );

	sb.safePrintf("</div>\n");

	sb.safePrintf("\n");
	sb.safePrintf("</form>\n");
	sb.safePrintf("<br>\n");
	sb.safePrintf("\n");


	if ( cr && cr->m_coll ) { // && strcmp(cr->m_coll,"main") ) {
		sb.safePrintf("<center>"
			      "Searching the <b>%s</b> collection."
			      "</center>",
			      cr->m_coll);
		sb.safePrintf("<br>\n");
		sb.safePrintf("\n");
	}


	// take this out for now
	/*
	// always the option to add event guru to their list of
	// search engine in their browser
	sb.safePrintf("<br>"
		      //"<br>"

		      "<script>\n"
		      "function addEngine() {\n"
		      "if (window.external && "
		      "('AddSearchProvider' in window.external)) {\n"
		      // Firefox 2 and IE 7, OpenSearch
		      "window.external.AddSearchProvider('http://"
		      "www.gigablast.com/searchbar.xml');\n"
		      "}\n"
		      "else if (window.sidebar && ('addSearchEngine' "
		      "in window.sidebar)) {\n"
		      // Firefox <= 1.5, Sherlock
		      "window.sidebar.addSearchEngine('http://"
		      "www.gigablast.com/searchbar.xml',"
		      //"example.com/search-plugin.src',"
		      "'http://www.gigablast.com/rocket.jpg'," //guru.png
		      "'Search Plugin', '');\n"
		      "}\n"
		      "else {"
		      // No search engine support (IE 6, Opera, etc).
		      "alert('No search engine support');\n"
		      "}\n"
		      // do not ask again if they tried to add it
		      // meta cookie should store this
		      //"document.getElementById('addedse').value='1';\n"
		      // NEVER ask again! permanent cookie
		      "document.cookie = 'didse=3';"
		      // make it invisible again
		      //"var e = document.getElementById('addse');\n"
		      //"e.style.display = 'none';\n"
		      "}\n"


		      "</script>\n"


		      "<center>"
		      "<a onclick='addEngine();' style="
		      "cursor:pointer;"
		      "cursor:hand;"
		      "color:blue;"
		      ">"

		      "<img height=16 width=16 border=0 src=/rocket16.png>"

		      "<font color=#505050>"
		      "%c%c%c "
		      "</font>"

		      "&nbsp; "

		      "Add Gigablast to your browser's "
		      "search engines"
		      "</a>"
		      "</center>"
		      "<br>"
		      "<br>"

		       // print triangle
		       ,0xe2
		       ,0x96
		       ,0xbc

		      );
	*/


	// print any red boxes we might need to
	if ( printRedBox2 ( &sb , sock , r ) ) // true ) )
		sb.safePrintf("<br>\n");

	sb.safePrintf("<br><center><table cellpadding=3>\n");
	sb.safePrintf("\n");

	char *root = "";
	if ( g_conf.m_isMattWells )
		root = "http://www.gigablast.com";

	sb.safePrintf("<tr valign=top>\n");

	//sb.safePrintf("<td align=center><div style=width:50px;height:50px;display:inline-block;background-color:red;></div></td>\n");
	sb.safePrintf("<td width=10%% "
		      "align=center><img style=padding-right:10px; "
		      "height=71px width=50px "
		      "src=%s/opensource.png></td>\n"
		      , root );

	sb.safePrintf("<td width=45%%><font size=+1><b>Open Source!</b>"
	"</font><br><br>\n");
	sb.brify2("Gigablast is now available as an <a href=https://github.com/gigablast/open-source-search-engine>open source search engine</a> on github.com. Download it today. Finally a robust, scalable search solution in C/C++ that has been in development and used commercially since 2000. <a href=http://www.gigablast.com/faq.html#features>Features</a>."
		  ,40);
	//sb.safePrintf("<br><br>");
	sb.safePrintf("</td>");

	sb.safePrintf("<td><font size=+1><b>ScreenShots</b>"
	"</font><br><br>\n");

	sb.safePrintf("<a href=/ss_settings.png><img width=150 height=81 src=ss_settings_thumb.png></a>");

	sb.safePrintf("<br><br>");

	sb.safePrintf("<a href=/ss_hosts.png><img width=150 height=81 src=ss_hosts_thumb.png></a>");

	sb.safePrintf("<br><br>");

	sb.safePrintf("<a href=/ss_filters.png><img width=150 height=81 src=ss_filters_thumb.png></a>");

	sb.safePrintf("</td>");


	sb.safePrintf("</tr>\n");

	sb.safePrintf("</table></center>\n");

	/*

	  do not show table for open source installs


	// donate with paypal
	sb.safePrintf("<tr valign=top>\n");
	sb.safePrintf("<td align=center style=padding-right:20px;><center>"

		      // BEGIN PAYPAL DONATE BUTTON
		      "<form action=\"https://www.paypal.com/cgi-bin/webscr\" method=\"post\" target=\"_top\">"
		      "<input type=\"hidden\" name=\"cmd\" value=\"_donations\">"
		      "<input type=\"hidden\" name=\"business\" value=\"2SFSFLUY3KS9Y\">"
		      "<input type=\"hidden\" name=\"lc\" value=\"US\">"
		      "<input type=\"hidden\" name=\"item_name\" value=\"Gigablast, Inc.\">"
		      "<input type=\"hidden\" name=\"currency_code\" value=\"USD\">"
		      "<input type=\"hidden\" name=\"bn\" value=\"PP-DonationsBF:btn_donateCC_LG.gif:NonHosted\">"
		      "<input type=\"image\" src=\"https://www.paypalobjects.com/en_US/i/btn/btn_donateCC_LG.gif\" border=\"0\" name=\"submit\" alt=\"PayPal - The safer, easier way to pay online!\" height=47 width=147>"
		      "<img alt=\"\" border=\"0\" src=\"https://www.paypalobjects.com/en_US/i/scr/pixel.gif\" width=\"1\" height=\"1\">"
		      "</form>"
		      // END PAYPAY BUTTON
		      "</center></div></center></td>\n"
		      );
	sb.safePrintf("<td><font size=+1><b>"
		      "Support Gigablast"
		      "</b></font><br>\n"
		      );
	sb.brify2(
		  "Donations of $100 or more receive a black "
		  "Gigablast T-shirt "
		  "with embroidered logo while quantities last. "
		  "State your address and size "
		  "in an <a href=/contact.html>email</a>. "
		  "PayPal accepted. "
		  "Help Gigablast continue "
		  "to grow and add new features."
		  , 80
		  );
	sb.safePrintf("</td></tr>\n");

	*/

	/*
	sb.safePrintf("<tr valign=top>\n");
	// 204x143
	sb.safePrintf("<td><img height=52px width=75px "
		      "src=%s/eventguru.png></td>\n"
		      , root );
	sb.safePrintf("<td><font size=+1><b>Event Guru Returns</b></font><br>\n");
	sb.brify2("<a href=http://www.eventguru.com/>Event Guru</a> datamines events from the web. It identifies events on a web page, or even plain text, using the same rules of deduction used by the human mind. It also has Facebook integration and lots of other cool things.",80);
	sb.safePrintf("<br><br></td></tr>\n");
	sb.safePrintf("\n");
	sb.safePrintf("\n");
	*/


	/*
	sb.safePrintf("<tr valign=top>\n");
	sb.safePrintf("<td align=center><div style=width:50px;height:50px;display:inline-block;background-color:green;></div></td>\n");
	sb.safePrintf("<td><font size=+1><b>The Green Search Engine</b></font><br>\n");
	sb.brify2("Gigablast is the only clean-powered web search engine. 90% of its power usage comes from wind energy. Astoundingly, Gigablast is one of ONLY four search engines in the United States indexing over a billion pages.",80);
	sb.safePrintf("<br><br></td></tr>\n");
	sb.safePrintf("\n");
	sb.safePrintf("\n");
	*/


	/*
	sb.safePrintf("<tr valign=top>\n");
	sb.safePrintf("<td align=center><img src=%s/gears.png "
		      "height=50 width=50></div></td>\n"
		      , root );
	sb.safePrintf("<td><font size=+1><b>The Transparent Search Engine</b></font><br>\n");
	sb.brify2("Gigablast is the first truly transparent search engine. It tells you exactly why the search results are ranked the way they are. There is nothing left to the imagination.",85);
	sb.safePrintf("<br><br>");
	sb.safePrintf("</td></tr>\n");
	sb.safePrintf("\n");
	sb.safePrintf("\n");
	*/

	/*
	if ( g_conf.m_isMattWells ) {
		sb.safePrintf("<tr valign=top>\n");
		sb.safePrintf("<td align=center><center><img src=%s/dollargear.png "
			      "height=50 width=50></center></div></center></td>\n"
			      , root );
		sb.safePrintf("<td><font size=+1><b>The SEO Search Engine</b></font><br>\n");
		sb.brify2("When it comes to search-engine based SEO, Gigablast is the place to be. With a frothy set of unique and effective <a href=http://www.gigablast.com/seo>SEO tools</a>, you will find all you need to execute a simple yet effective SEO strategy. Stop the guesswork, and let a search engine tell you how to SEO it.",85);
		sb.safePrintf("</td></tr>\n");
	}
	*/

	/*
	sb.safePrintf("<tr valign=top>\n");
	sb.safePrintf("<td><div style=width:50px;height:50px;display:inline-block;background-color:ff3030;></td>\n");
	sb.safePrintf("<td><font size=+1><b>Xml Search Feed</b></font><br>\n");
	sb.brify2("Utilize Gigablast's results on your own site or product by connecting with Gigablast's <a href=/searchfeed.html>XML search feed</a>. It's now simpler than ever to setup and use. You can also add the web pages you want into the index in near real-time.",85);
	sb.safePrintf("</td></tr>\n");
	*/

	/*
	sb.safePrintf("<tr valign=top>\n");
	sb.safePrintf("<td><div style=width:50px;height:50px;display:inline-block;background-color:black;></td>\n");
	sb.safePrintf("<td><font size=+1><b>The Private Search Engine</b>"
	"</font><br>\n");
	sb.brify2("Gigablast does not allow the NSA or any third party "
		  "to spy on the queries your IP address is doing, "
		  "unlike "
		  "<a href=http://www.guardian.co.uk/world/2013/jun/"
		  "06/us-tech-giants-nsa-data>"
		  "other large search engines</a>. "
		  "Gigablast is the only "
		  "<a href=/privacy.html>truly private search engine</a> "
		  "in the United States."
		  //" Everyone else has fundamental "
		  //"gaps in their "
		  //"security as explained by the above link."
		  //"Tell Congress "
		  //"to <a href=https://optin.stopwatching.us/>stop spying "
		  //"on you</a>."
		  ,85);
	sb.safePrintf("</td></tr>\n");
	*/

	/*
	sb.safePrintf("<tr valign=top>\n");
	sb.safePrintf("<td><div style=width:50px;height:50px;display:inline-block;background-color:black;></td>\n");
	sb.safePrintf("<td><font size=+1><b>No Tax Dodging</b></font><br>\n");
	sb.brify2("Gigablast pays its taxes when it makes a profit. "
		  "Google and Bing <a href=http://www.bloomberg.com/news/"
		  "2010-10-21/google-2-4-rate-shows-how-60-billion-u-s-"
		  "revenue-lost-to-tax-loopholes.html>do not</a>. They "
		  "stash their profits in "
		  "offshore tax havens to avoid paying taxes. "
		  //"The end result is that taxes are higher for you. "
		  "You may think Google and Bing are free to use, but in "
		  "reality, <u>you</u> pay for it in increased taxes."
		  ,85);
	sb.safePrintf("</td></tr>\n");
	*/


	//
	// begin new stuff
	//
	/*
	// gradients
	sb.safePrintf("<style><!--\n");
	
	sb.safePrintf(".grad {");
	sb.safePrintf("background: rgb(190,201,247);");
	sb.safePrintf("background: url(data:image/svg+xml;base64,PD94bWwgdmVyc2lvbj0iMS4wIiA/Pgo8c3ZnIHhtbG5zPSJodHRwOi8vd3d3LnczLm9yZy8yMDAwL3N2ZyIgd2lkdGg9IjEwMCUiIGhlaWdodD0iMTAwJSIgdmlld0JveD0iMCAwIDEgMSIgcHJlc2VydmVBc3BlY3RSYXRpbz0ibm9uZSI+CiAgPGxpbmVhckdyYWRpZW50IGlkPSJncmFkLXVjZ2ctZ2VuZXJhdGVkIiBncmFkaWVudFVuaXRzPSJ1c2VyU3BhY2VPblVzZSIgeDE9IjAlIiB5MT0iMCUiIHgyPSIxMDAlIiB5Mj0iMTAwJSI+CiAgICA8c3RvcCBvZmZzZXQ9IjAlIiBzdG9wLWNvbG9yPSIjYmVjOWY3IiBzdG9wLW9wYWNpdHk9IjEiLz4KICAgIDxzdG9wIG9mZnNldD0iMTAwJSIgc3RvcC1jb2xvcj0iIzBiM2NlZCIgc3RvcC1vcGFjaXR5PSIxIi8+CiAgPC9saW5lYXJHcmFkaWVudD4KICA8cmVjdCB4PSIwIiB5PSIwIiB3aWR0aD0iMSIgaGVpZ2h0PSIxIiBmaWxsPSJ1cmwoI2dyYWQtdWNnZy1nZW5lcmF0ZWQpIiAvPgo8L3N2Zz4=);");
	sb.safePrintf("background: -moz-linear-gradient(-45deg, rgba(190,201,247,1) 0%%, rgba(11,60,237,1) 100%%);");
	sb.safePrintf("background: -webkit-gradient(linear, left top, right bottom, color-stop(0%%,rgba(190,201,247,1)), color-stop(100%%,rgba(11,60,237,1)));");
	sb.safePrintf("background: -webkit-linear-gradient(-45deg, rgba(190,201,247,1) 0%%,rgba(11,60,237,1) 100%%);");
	sb.safePrintf("background: -o-linear-gradient(-45deg, rgba(190,201,247,1) 0%%,rgba(11,60,237,1) 100%%);");
	sb.safePrintf("background: -ms-linear-gradient(-45deg, rgba(190,201,247,1) 0%%,rgba(11,60,237,1) 100%%);");
	sb.safePrintf("background: linear-gradient(135deg, rgba(190,201,247,1) 0%%,rgba(11,60,237,1) 100%%);");
	sb.safePrintf("filter: progid:DXImageTransform.Microsoft.gradient( startColorstr='#bec9f7', endColorstr='#0b3ced',GradientType=1 );");
	sb.safePrintf("}");
	sb.safePrintf("-->");
	sb.safePrintf("</style>\n");

	sb.safePrintf("<br>");


	sb.safePrintf("<div class=grad style=\"border-radius:200px;border-color:blue;border-style:solid;border-width:3px;padding:12px;width:320px;height:320px;display:inline-block;z-index:100;color:black;position:relative;background-color:lightgray;\">");

	sb.safePrintf("<br>");
	sb.safePrintf("<b>");

	sb.safePrintf("<font style=font-size:18px;margin-left:80px;>");
	sb.safePrintf("Build Your Own");
	sb.safePrintf("</font>");
	sb.safePrintf("<br>");
	sb.safePrintf("<font style=font-size:18px;margin-left:80px;>");
	sb.safePrintf("Search Engine in the");
	sb.safePrintf("</font>");
	sb.safePrintf("<br>");
	sb.safePrintf("<font style=font-size:18px;margin-left:80px;>");
	sb.safePrintf("Cloud");
	sb.safePrintf("</font>");
	sb.safePrintf("</b>");

	sb.safePrintf("<br>");
	sb.safePrintf("<br>");

	sb.safePrintf("<div style=margin-left:20px;width:270px;>");
	sb.safePrintf("<a href=/admin/addcoll><img style=float:left;padding-right:15px; height=188px width=101px src=/robot3.png></a>");
	//sb.safePrintf("<br>");
	sb.safePrintf("<b>STEP 1.</b> <a href=/admin/addcoll?guide=1>"
		      "Click here to");
	sb.safePrintf("<br>");
	sb.safePrintf("<b>name your engine</b></a>.");
	sb.safePrintf("<br>");
	sb.safePrintf("<br>");
	sb.safePrintf("<b>STEP 2.</b> <a href=/admin/settings?guide=1>"
		      "Click here to ");
	sb.safePrintf("<br>");
	sb.safePrintf("<b>add websites to index</b></a>.");
	sb.safePrintf("<br>");
	sb.safePrintf("<br>");
	sb.safePrintf("<b>STEP 3.</b> <a href=/widgets.html?guide=1>"
		      "Click here to");
	sb.safePrintf("<br>");
	sb.safePrintf("<b>insert search box</b></a>.");

	sb.safePrintf("</div>");

	sb.safePrintf("</div>");
	*/

	/*

	sb.safePrintf("<div class=grad style=\"border-radius:200px;border-color:blue;border-style:solid;border-width:3px;padding:12px;width:280px;height:280px;display:inline-block;z-index:105;color:black;margin-left:-50px;position:absolute;margin-top:50px;background-color:lightgray;\">");

	sb.safePrintf("<br>");
	sb.safePrintf("<br style=line-height:25px;>");
	sb.safePrintf("<b>");
	sb.safePrintf("<font style=font-size:18px;margin-left:40px;>");
	sb.safePrintf("Web Search Appliance");
	sb.safePrintf("</font>");
	sb.safePrintf("<br>");
	sb.safePrintf("<br>");
	sb.safePrintf("<br>");
	sb.safePrintf("</b>");


	sb.safePrintf("<div style=margin-left:20px;width:270px;>");
	sb.safePrintf("<a href=http://www.gigablast.com/appliance.html><img style=float:left;padding-bottom:20px;padding-right:10px; height=81px width=121px src=/computer2.png></a>");


	sb.safePrintf("Put the web in your closet. ");
	sb.safePrintf("Jump start your efforts with four 1U supermicro servers loaded with the top 2 billion pages from the web. <a href=http://www.gigablast.com/appliance.html>[learn more]</a>");
	sb.safePrintf("</font>");

	sb.safePrintf("</div>");

	sb.safePrintf("</div>");
	*/
	/*

	sb.safePrintf("<div class=grad style=\"border-radius:300px;border-color:blue;border-style:solid;border-width:3px;padding:12px;width:240px;height:240px;display:inline-block;z-index:110;color:black;margin-left:-240px;position:absolute;margin-top:230px;background-color:lightgray;\">");

	sb.safePrintf("<br>");
	sb.safePrintf("<b>");
	sb.safePrintf("<font style=font-size:18px;margin-left:60px;>");
	sb.safePrintf("Open Source");
	sb.safePrintf("</font>");
	sb.safePrintf("<br>");
	sb.safePrintf("<br>");
	sb.safePrintf("</b>");

	sb.safePrintf("<div style=margin-left:30px;margin-right:5px;>");
	sb.safePrintf("<a href=http://www.gigablast.com/faq.html#features><img style=float:left;padding-right:10px height=71px width=71px src=/unlocked2.png></a>");

	sb.safePrintf("Gigablast is now available as an <a href=https://github.com/gigablast/open-source-search-engine>open source search engine</a> on github.com. Download it today. Finally a robust, scalable search solution in C/C++ that has been in development and used commercially since 2000. <a href=http://www.gigablast.com/faq.html#features>Features</a>.");
	sb.safePrintf("</div>");

	sb.safePrintf("</div>");

	//
	// donate with paypal bubble
	//

	sb.safePrintf("<div class=grad style=\"border-radius:300px;border-color:blue;border-style:solid;border-width:3px;padding:12px;width:180px;height:180px;display:inline-block;z-index:120;color:black;margin-left:10px;position:absolute;margin-top:270px;background-color:lightgray;\">");

	sb.safePrintf("<br>");
	sb.safePrintf("<b>");
	sb.safePrintf("<font style=font-size:18px;margin-left:40px;>");
	sb.safePrintf("Contribute");
	sb.safePrintf("</font>");
	sb.safePrintf("<br>");
	sb.safePrintf("<br>");
	sb.safePrintf("</b>");

	sb.safePrintf("<div style=margin-left:15px;margin-right:5px;>");


	sb.safePrintf(

		      "Help Gigablast development with PayPal."
		      "<br>"
		      "<br>"
		      // BEGIN PAYPAL DONATE BUTTON
		      "<form action=\"https://www.paypal.com/cgi-bin/webscr\" method=\"post\" target=\"_top\">"
		      "<input type=\"hidden\" name=\"cmd\" value=\"_donations\">"
		      "<input type=\"hidden\" name=\"business\" value=\"2SFSFLUY3KS9Y\">"
		      "<input type=\"hidden\" name=\"lc\" value=\"US\">"
		      "<input type=\"hidden\" name=\"item_name\" value=\"Gigablast, Inc.\">"
		      "<input type=\"hidden\" name=\"currency_code\" value=\"USD\">"
		      "<input type=\"hidden\" name=\"bn\" value=\"PP-DonationsBF:btn_donateCC_LG.gif:NonHosted\">"
		      "<input type=\"image\" src=\"https://www.paypalobjects.com/en_US/i/btn/btn_donateCC_LG.gif\" border=\"0\" name=\"submit\" alt=\"PayPal - The safer, easier way to pay online!\" height=47 width=147>"
		      "<img alt=\"\" border=\"0\" src=\"https://www.paypalobjects.com/en_US/i/scr/pixel.gif\" width=\"1\" height=\"1\">"
		      "</form>"
		      // END PAYPAY BUTTON
		      "</center></div></center>"
		      //"</td>\n"
		      );
	*/
	//
	// end new stuff
	//


	sb.safePrintf("\n");
	sb.safePrintf("\n");
	//sb.safePrintf("</table>\n");
	sb.safePrintf("<br><br>\n");
	printNav ( sb , r );
	return true;
}

bool printAddUrlHomePage ( SafeBuf &sb , char *url , HttpRequest *r ) {

	CollectionRec *cr = g_collectiondb.getRec ( r );

	printFrontPageShell ( &sb , "add url" , cr , true );


	sb.safePrintf("<script type=\"text/javascript\">\n"
		      "function handler() {\n" 
		      "if(this.readyState == 4 ) {\n"
		      "document.getElementById('msgbox').innerHTML="
		      "this.responseText;\n"
		      //"alert(this.status+this.statusText+"
		      //"this.responseXML+this.responseText);\n"
		      "}}\n"
		      "</script>\n");


	sb.safePrintf("<br><br>\n");
	sb.safePrintf("<br><br><br>\n");
	/*
	sb.safePrintf("<b>web</b> &nbsp;&nbsp;&nbsp;&nbsp; ");
	if ( g_conf.m_isMattWells )
		sb.safePrintf("<a href=http://www.gigablast.com/seo>seo</a> "
			      "&nbsp;&nbsp;&nbsp;&nbsp; "
			      );
	sb.safePrintf( "<a href=\"/Top\">directory</a> "
		      "&nbsp;&nbsp;&nbsp;&nbsp; \n");
	sb.safePrintf("<a href=/adv.html>advanced search</a>");
	sb.safePrintf(" &nbsp;&nbsp;&nbsp;&nbsp; ");
	sb.safePrintf("<a href=/addurl title=\"Instantly add your url to "
		      "Gigablast's index\">add url</a>");
	sb.safePrintf("\n");
	sb.safePrintf("<br><br>\n");
	*/
	// submit to https now
	sb.safePrintf("<form method=GET "
		      "action=/addurl name=f>\n" );

	char *coll = "";
	if ( cr ) coll = cr->m_coll;
	if ( cr )
		sb.safePrintf("<input type=hidden name=c value=\"%s\">",
			      cr->m_coll);


	// put search box in a box
	sb.safePrintf("<div style="
		      "background-color:#%s;" // fcc714;"
		      "border-style:solid;"
		      "border-width:3px;"
		      "border-color:blue;"
		      //"background-color:blue;"
		      "padding:20px;"
		      "border-radius:20px;"
		      ">"
		      , GOLD
		      );


	sb.safePrintf("<input name=urls type=text "
		      "style=\""
		      //"width:%"INT32"px;"
		      "height:26px;"
		      "padding:0px;"
		      "font-weight:bold;"
		      "padding-left:5px;"
		      //"border-radius:10px;"
		      "margin:0px;"
		      "border:1px inset lightgray;"
		      "background-color:#ffffff;"
		      "font-size:18px;"
		      "\" "

		      "size=40 value=\""
		      );



	if ( url ) {
		SafeBuf tmp;
		tmp.safePrintf("%s",url);
		// don't let double quotes in the url close our val attribute
		tmp.replace("\"","%22");
		sb.safeMemcpy(&tmp);
	}
	else
		sb.safePrintf("http://");
	sb.safePrintf("\">&nbsp; &nbsp;"
		      //"<input type=\"submit\" value=\"Add Url\">\n"
		      "<div onclick=document.f.submit(); "


		      " onmouseover=\""
		      "this.style.backgroundColor='lightgreen';"
		      "this.style.color='black';\""
		      " onmouseout=\""
		      "this.style.backgroundColor='green';"
		      "this.style.color='white';\" "

		      "style=border-radius:28px;"
		      "cursor:pointer;"
		      "cursor:hand;"
		      "border-color:white;"
		      "border-style:solid;"
		      "border-width:3px;"
		      "padding:12px;"
		      "width:20px;"
		      "height:20px;"
		      "display:inline-block;"
		      "background-color:green;color:white;>"
		      "<b style=margin-left:-5px;font-size:18px;>GO</b>"
		      "</div>"
		      "\n"
		      );
	sb.safePrintf("\n");


	sb.safePrintf("</div>\n");

	sb.safePrintf("\n");
	sb.safePrintf("<br>\n");
	sb.safePrintf("\n");


	// if addurl is turned off, just print "disabled" msg
	char *msg = NULL;
	if ( ! g_conf.m_addUrlEnabled ) 
		msg = "Add url is temporarily disabled";
	// can also be turned off in the collection rec
	//if ( ! cr->m_addUrlEnabled    ) 
	//	msg = "Add url is temporarily disabled";
	// or if in read-only mode
	if (   g_conf.m_readOnlyMode  ) 
		msg = "Add url is temporarily disabled";

	sb.safePrintf("<br><center>"
		      "Add a url to the <b>%s</b> collection</center>",coll);

	// if url is non-empty the ajax will receive this identical msg
	// and display it in the div, so do not duplicate the msg!
	if ( msg && ! url )
		sb.safePrintf("<br><br>%s",msg);


	// . the ajax msgbox div
	// . when loaded with the main page for the first time it will
	//   immediately replace its content...
	if ( url ) {
		char *root = "";
		if ( g_conf.m_isMattWells )
			root = "http://www.gigablast.com";
		sb.safePrintf("<br>"
			      "<br>"
			      "<div id=msgbox>"
			      //"<b>Injecting your url. Please wait...</b>"
			      "<center>"
			      "<img src=%s/gears.gif width=50 height=50>"
			      "</center>"
			      "<script type=text/javascript>"
			      //"alert('shit');"
			      "var client = new XMLHttpRequest();\n"
			      "client.onreadystatechange = handler;\n"
			      "var url='/addurl?urls="
			      , root );
		sb.urlEncode ( url );
		// propagate "admin" if set
		//int32_t admin = hr->getLong("admin",-1);
		//if ( admin != -1 ) sb.safePrintf("&admin=%"INT32"",admin);
		// provide hash of the query so clients can't just pass in
		// a bogus id to get search results from us
		uint32_t h32 = hash32n(url);
		if ( h32 == 0 ) h32 = 1;
		uint64_t rand64 = gettimeofdayInMillisecondsLocal();
		// msg7 needs an explicit collection for /addurl for injecting
		// in PageInject.cpp. it does not use defaults for safety.
		sb.safePrintf("&id=%"UINT32"&c=%s&rand=%"UINT64"';\n"
			      "client.open('GET', url );\n"
			      "client.send();\n"
			      "</script>\n"
			      , h32
			      , coll
			      , rand64
			      );
		sb.safePrintf("</div>\n");
	}

	sb.safePrintf("</form>\n");
	sb.safePrintf("<br>\n");
	sb.safePrintf("\n");
	sb.safePrintf("<br><br>\n");

	printNav ( sb , r );
	return true;
}


bool printDirHomePage ( SafeBuf &sb , HttpRequest *r ) {

	char format = r->getReplyFormat();
	if ( format != FORMAT_HTML )
		return printTopDirectory ( sb , format );

	CollectionRec *cr = g_collectiondb.getRec ( r );

	printFrontPageShell ( &sb , "directory" , cr , true );

	sb.safePrintf("<br><br>\n");
	sb.safePrintf("<br><br><br>\n");

	// submit to https now
	sb.safePrintf("<form method=GET "
		      "action=/search name=f>\n");

	if ( cr )
		sb.safePrintf("<input type=hidden name=c value=\"%s\">",
			      cr->m_coll);


	// put search box in a box
	sb.safePrintf("<div style="
		      "background-color:#%s;" // fcc714;"
		      "border-style:solid;"
		      "border-width:3px;"
		      "border-color:blue;"
		      //"background-color:blue;"
		      "padding:20px;"
		      "border-radius:20px;"
		      ">"
		      ,GOLD
		      );


	sb.safePrintf("<input name=q type=text "
		      "style=\""
		      //"width:%"INT32"px;"
		      "height:26px;"
		      "padding:0px;"
		      "font-weight:bold;"
		      "padding-left:5px;"
		      //"border-radius:10px;"
		      "margin:0px;"
		      "border:1px inset lightgray;"
		      "background-color:#ffffff;"
		      "font-size:18px;"
		      "\" "

		      "size=40 value=\"\">&nbsp; &nbsp;"

		      //"<input type=\"submit\" value=\"Search\">\n");

		      "<div onclick=document.f.submit(); "

		      " onmouseover=\""
		      "this.style.backgroundColor='lightgreen';"
		      "this.style.color='black';\""
		      " onmouseout=\""
		      "this.style.backgroundColor='green';"
		      "this.style.color='white';\" "

		      "style=border-radius:28px;"
		      "cursor:pointer;"
		      "cursor:hand;"
		      "border-color:white;"
		      "border-style:solid;"
		      "border-width:3px;"
		      "padding:12px;"
		      "width:20px;"
		      "height:20px;"
		      "display:inline-block;"
		      "background-color:green;color:white;>"
		      "<b style=margin-left:-5px;font-size:18px;>GO</b>"
		      "</div>"
		      "\n"
		      );

	sb.safePrintf("</div>\n");

	sb.safePrintf("\n");
	sb.safePrintf("</form>\n");
	sb.safePrintf("<br>\n");
	sb.safePrintf("\n");


	printTopDirectory ( sb , FORMAT_HTML );

	sb.safePrintf("<br><br>\n");

	printNav ( sb , r);

	return true;
}


// . returns false if blocked, true otherwise
// . sets errno on error
// . make a web page displaying the config of this host
// . call g_httpServer.sendDynamicPage() to send it
bool sendPageRoot ( TcpSocket *s , HttpRequest *r, char *cookie ) {
	// don't allow pages bigger than 128k in cache
	char  buf [ 10*1024 ];//+ MAX_QUERY_LEN ];
	// a ptr into "buf"
	//char *p    = buf;
	//char *pend = buf + 10*1024 + MAX_QUERY_LEN - 100 ;
	SafeBuf sb(buf, 10*1024 );//+ MAX_QUERY_LEN);
	// print bgcolors, set focus, set font style
	//p = g_httpServer.printFocus  ( p , pend );
	//p = g_httpServer.printColors ( p , pend );
	//int32_t  qlen;
	//char *q = r->getString ( "q" , &qlen , NULL );
	// insert collection name too
	CollectionRec *cr = g_collectiondb.getRec(r);
	if ( ! cr ) {
		g_errno = ENOCOLLREC;
		return g_httpServer.sendErrorReply(s,500,mstrerror(g_errno)); 
	}


	// get the collection rec
	/*
	CollectionRec *cr = g_collectiondb.getRec ( coll );
	uint8_t *hp = NULL;
	int32_t  hpLen;
	int64_t  docsInColl = -1;
	if ( ! cr ) {
		// use the default 
		Parm *pp = g_parms.getParm ( "hp" );
		if ( ! pp ) {
			g_errno = ENOTFOUND;
			g_msg = " (error: no such collection)";		
			return g_httpServer.sendErrorReply(s,500,
							   mstrerror(g_errno));
		}
		hp       = (uint8_t*)pp->m_def;
		if ( hp ) hpLen = uint8strlen ( hp );
		if ( hpLen <= 0 || ! hp )
			log(LOG_INFO,"http: No root page html present.");
	} else {
		if(cr->m_useLanguagePages) {
			uint8_t lang = g_langId.guessGBLanguageFromUrl(r->getHost());
			if(lang && (hp = g_languagePages.getLanguagePage(lang)) != NULL) {
					hpLen = uint8strlen(hp);
					// Set sort language as well
					// This might not be a good idea, as it
					// overrides any other setting. May be
					// better to let the user agent string
					// tell us what the user wants.
					strcpy(cr->m_defaultSortLanguage,
							getLanguageAbbr(lang));
			}
		}
		if(!hp) {
			hp    = (uint8_t*)cr->m_htmlRoot;
			hpLen = cr->m_htmlRootLen;
		}
		//RdbBase *base = getRdbBase ( RDB_CHECKSUMDB , coll );
		RdbBase *base = getRdbBase ( (uint8_t)RDB_CLUSTERDB , coll );
		if ( base ) docsInColl = base->getNumGlobalRecs();
	}
	*/
	// print the page out
	/*
	expandRootHtml     ( sb, 
			     hp , hpLen ,
			     q , qlen , r , s , docsInColl ,
			     cr );
	*/


	//if ( ! strcmp(coll,"dmoz" ) )
	//	printDirHomePage(sb,r);
	//else
	printWebHomePage(sb,r,s);


	// . print last 5 queries
	// . put 'em in a table
	// . disable for now, impossible to monitor/control
	//p += printLastQueries ( p , pend );
	// are we the admin?
	//bool isAdmin = g_collectiondb.isAdmin ( r , s );

	// calculate bufLen
	//int32_t bufLen = p - buf;
	// . now encapsulate it in html head/tail and send it off
	// . the 0 means browser caches for however int32_t it's set for
	// . but we don't use 0 anymore, use -2 so it never gets cached so
	//   our display of the # of pages in the index is fresh
	// . no, but that will piss people off, its faster to keep it cached
	//return g_httpServer.sendDynamicPage ( s , buf , bufLen , -1 );
	return g_httpServer.sendDynamicPage ( s,
					      (char*) sb.getBufStart(),
					      sb.length(),
					      // 120 seconds cachetime
					      // don't cache anymore since
					      // we have the login bar at
					      // the top of the page
					      0,//120, // cachetime
					      false,// post?
					      "text/html",
					      200,
					      NULL, // cookie
					      "UTF-8",
					      r);
}

// . store into "p"
// . returns bytes stored into "p"
// . used for entertainment purposes
/*
int32_t printLastQueries ( char *p , char *pend ) {
	// if not 512 bytes left, bail
	if ( pend - p < 512 ) return 0;
	// return w/ no table if no queries have been added to g_qbuf yet
	if ( ! g_nextq == -1 ) return 0;
	// remember start for returning # of bytes stored
	char *start = p;
	// begin table (no border)
	sprintf (p,"<br><table border=0><tr><td><center>Last %"INT32" queries:"
		 "</td></tr>", (int32_t)QBUF_NUMQUERIES );
	p += gbstrlen ( p );		
	// point to last query added
	int32_t n = g_nextq - 1;
	// . wrap it if we need to
	// . QBUF_NUMQUERIES is defined to be 5 in PageResults.h
	if ( n < 0 ) n = QBUF_NUMQUERIES - 1;
	// . print up to five queries
	// . queries are stored by advancing g_nextq, so "i" should go backward
	int32_t count = 0;
	for ( int32_t i = n ; count < QBUF_NUMQUERIES ; count++ , i-- ) {
		// wrap i if we need to
		if ( i == -1 ) i = QBUF_NUMQUERIES - 1;
		// if this query is empty, skip it (might be uninitialized)
		if ( g_qbuf[i][0] == '\0' ) continue;
		// point to the query (these are NULL terminated)
		char *q    = g_qbuf[i];
		int32_t  qlen = gbstrlen(q);
		// bail if too big 
		if ( p + qlen + 32 + 1024 >= pend ) return p - start;
		// otherwise, print this query to the page
		sprintf ( p , "<tr><td><a href=/cgi/0.cgi?q=" );
		p += gbstrlen ( p );
		// store encoded query as cgi parm
		p += urlEncode ( p , q , qlen );
		// end a href tag
		*p++ = '>';
		// . then print the actual query to the page
		// . use htmlEncode so nobody can abuse it
		p += saftenTags ( p , pend - p , q , qlen );
		// wrap it up
		sprintf ( p , "</a></td></tr>" );
		p += gbstrlen ( p );		
	}
	// end the table
	sprintf ( p , "</table>");
	p += gbstrlen ( p );
	// return bytes written
	return p - start;
}
*/


//char *printTopDirectory ( char *p, char *pend ) {
bool printTopDirectory ( SafeBuf& sb , char format ) {

	int32_t nr = g_catdb.getRdb()->getNumTotalRecs();

	// if no recs in catdb, print instructions
	if ( nr == 0 && format == FORMAT_HTML)
		return sb.safePrintf("<center>"
				     "<b>DMOZ functionality is not set up.</b>"
				     "<br>"
				     "<br>"
				     "<b>"
				     "Please follow the set up "
				     "<a href=/faq.html#dmoz>"
				     "instructions"
				     "</a>."
				     "</b>"
				     "</center>");

	// send back an xml/json error reply
	if ( nr == 0 && format != FORMAT_HTML ) {
		g_errno = EDMOZNOTREADY;
		return false;
	}

	//char topList[4096];
	//sprintf(topList, 
	return sb.safePrintf (
	"<center>"
	"<table cellspacing=\"4\" cellpadding=\"4\"><tr><td valign=top>\n"
	"<b><a href=\"/Top/Arts/\">Arts</a></b><br>"
	"<small>"
	"<a href=\"/Top/Arts/Movies/\">Movies</a>, "
	"<a href=\"/Top/Arts/Television/\">Television</a>, "
	"<a href=\"/Top/Arts/Music/\">Music</a>..."
	"</small>\n"
	"</td><td valign=top>"
	"<b><a href=\"/Top/Business/\">Business</a></b><br>"
	"<small>"
	"<a href=\"/Top/Business/Employment/\">Jobs</a>, "
	"<a href=\"/Top/Business/Real_Estate/\">Real Estate</a>, "
	"<a href=\"/Top/Business/Investing/\">Investing</a>..."
	"</small>\n"
	"</td><td valign=top>"
	"<b><a href=\"/Top/Computers/\">Computers</a></b><br>"
	"<small>"
	"<a href=\"/Top/Computers/Internet/\">Internet</a>, "
	"<a href=\"/Top/Computers/Software/\">Software</a>, "
	"<a href=\"/Top/Computers/Hardware/\">Hardware</a>..."
	"</small>\n"
	"</td></tr><tr><td valign=top>"
	"<b><a href=\"/Top/Games/\">Games</a></b><br>"
	"<small>"
	"<a href=\"/Top/Games/Video_Games/\">Video Games</a>, "
	"<a href=\"/Top/Games/Roleplaying/\">RPGs</a>, "
	"<a href=\"/Top/Games/Gambling/\">Gambling</a>..."
	"</small>\n"
	"</td><td valign=top>"
	"<b><a href=\"/Top/Health/\">Health</a></b><br>"
	"<small>"
	"<a href=\"/Top/Health/Fitness/\">Fitness</a>, "
	"<a href=\"/Top/Health/Medicine/\">Medicine</a>, "
	"<a href=\"/Top/Health/Alternative/\">Alternative</a>..."
	"</small>\n"
	"</td><td valign=top>"
	"<b><a href=\"/Top/Home/\">Home</a></b><br>"
	"<small>"
	"<a href=\"/Top/Home/Family/\">Family</a>, "
	"<a href=\"/Top/Home/Consumer_Information/\">Consumers</a>, "
	"<a href=\"/Top/Home/Cooking/\">Cooking</a>..."
	"</small>\n"
	"</td></tr><tr><td valign=top>"
	//"<b><a href=\"/Top/Kids_and_Teens/\">"
	//"<font color=\"#ff0000\">K</font>"
	//"<font color=\"339900\">i</font>"
	//"<font color=\"#ff6600\">d</font>"
	//"<font color=\"#0066ff\">s</font>"
	//" and Teens</a></b><br>"
	"<b><a href=\"/Top/Kids_and_Teens/\">Kids and Teens</a></b><br>"
	"<small>"
	"<a href=\"/Top/Kids_and_Teens/Arts/\">Arts</a>, "
	"<a href=\"/Top/Kids_and_Teens/School_Time/\">School Time</a>, "
	"<a href=\"/Top/Kids_and_Teens/Teen_Life/\">Teen Life</a>..."
	"</small>\n"
	"</td><td valign=top>"
	"<b><a href=\"/Top/News/\">News</a></b><br>"
	"<small>"
	"<a href=\"/Top/News/Media/\">Media</a>, "
	"<a href=\"/Top/News/Newspapers/\">Newspapers</a>, "
	"<a href=\"/Top/News/Weather/\">Weather</a>..."
	"</small>\n"
	"</td><td valign=top>"
	"<b><a href=\"/Top/Recreation/\">Recreation</a></b><br>"
	"<small>"
	"<a href=\"/Top/Recreation/Travel/\">Travel</a>, "
	"<a href=\"/Top/Recreation/Food/\">Food</a>, "
	"<a href=\"/Top/Recreation/Outdoors/\">Outdoors</a>, "
	"<a href=\"/Top/Recreation/Humor/\">Humor</a>..."
	"</small>\n"
	"</td></tr><tr><td valign=top>"
	"<b><a href=\"/Top/Reference/\">Reference</a></b><br>"
	"<small>"
	"<a href=\"/Top/Reference/Maps/\">Maps</a>, "
	"<a href=\"/Top/Reference/Education/\">Education</a>, "
	"<a href=\"/Top/Reference/Libraries/\">Libraries</a>..."
	"</small>\n"
	"</td><td valign=top>"
	"<b><a href=\"/Top/Regional/\">Regional</a></b><br>"
	"<small>"
	"<a href=\"/Top/Regional/North_America/United_States/\">US</a>, "
	"<a href=\"/Top/Regional/North_America/Canada/\">Canada</a>, "
	"<a href=\"/Top/Regional/Europe/United_Kingdom/\">UK</a>, "
	"<a href=\"/Top/Regional/Europe/\">Europe</a>..."
	"</small>\n"
	"</td><td valign=top>"
	"<b><a href=\"/Top/Science/\">Science</a></b><br>"
	"<small>"
	"<a href=\"/Top/Science/Biology/\">Biology</a>, "
	"<a href=\"/Top/Science/Social_Sciences/Psychology/\">Psychology</a>, "
	"<a href=\"/Top/Science/Physics/\">Physics</a>..."
	"</small>\n"
	"</td></tr><tr><td valign=top>"
	"<b><a href=\"/Top/Shopping/\">Shopping</a></b><br>"
	"<small>"
	"<a href=\"/Top/Shopping/Vehicles/Autos/\">Autos</a>, "
	"<a href=\"/Top/Shopping/Clothing/\">Clothing</a>, "
	"<a href=\"/Top/Shopping/Gifts/\">Gifts</a>..."
	"</small>\n"
	"</td><td valign=top>"
	"<b><a href=\"/Top/Society/\">Society</a></b><br>"
	"<small>"
	"<a href=\"/Top/Society/People/\">People</a>, "
	"<a href=\"/Top/Society/Religion_and_Spirituality/\">Religion</a>, "
	"<a href=\"/Top/Society/Issues/\">Issues</a>..."
	"</small>\n"
	"</td><td valign=top>"
	"<b><a href=\"/Top/Sports/\">Sports</a></b><br>"
	"<small>"
	"<a href=\"/Top/Sports/Baseball/\">Baseball</a>, "
	"<a href=\"/Top/Sports/Soccer/\">Soccer</a>, "
	"<a href=\"/Top/Sports/Basketball/\">Basketball</a>..."
	"</small>\n"
	"</td></tr>"
	"<tr><td colspan=3 valign=top>"
	"<b><a href=\"/Top/World/\">World</a></b><br>"
	"<small>"
	"<a href=\"/Top/World/Deutsch/\">Deutsch</a>, "
	"<a href=\"/Top/World/Espa%%c3%%b1ol/\">Espa%c%col</a>, "
	"<a href=\"/Top/World/Fran%%c3%%a7ais/\">Fran%c%cais</a>, "
	"<a href=\"/Top/World/Italiano/\">Italiano</a>, "
	"<a href=\"/Top/World/Japanese/\">Japanese</a>, "
	"<a href=\"/Top/World/Nederlands/\">Nederlands</a>, "
	"<a href=\"/Top/World/Polska/\">Polska</a>, "
	"<a href=\"/Top/World/Dansk/\">Dansk</a>, "
	"<a href=\"/Top/World/Svenska/\">Svenska</a>..."
	"</small>\n"
	"</td></tr></table></center>\n",
	195, 177, 195, 167);
	// make sure there's room
	//int32_t topListLen = gbstrlen(topList);
	//if (pend - p <= topListLen+1)
	//	return p;
	// copy it in
	//gbmemcpy(p, topList, topListLen);
	//p += topListLen;
	//*p = '\0';
	//return p;
}

/////////////////
//
// ADD URL PAGE
//
/////////////////

#include "PageInject.h"
#include "TuringTest.h"
#include "AutoBan.h"
//#include "CollectionRec.h"
#include "Users.h"
#include "Spider.h"

//static bool sendReply        ( void *state  , bool addUrlEnabled );
static bool canSubmit        (uint32_t h, int32_t now, int32_t maxUrlsPerIpDom);

//static void addedStuff ( void *state );

void resetPageAddUrl ( ) ;

/*
class State2 {
public:
	Url        m_url;
	//char      *m_buf;
	//int32_t       m_bufLen;
	//int32_t       m_bufMaxLen;
};
*/

class State1i {
public:
	//Msg4       m_msg4;
	Msg7       m_msg7;
	TcpSocket *m_socket;
        bool       m_isMasterAdmin;
	char       m_coll[MAX_COLL_LEN+1];
	bool       m_goodAnswer;
	bool       m_doTuringTest;
	int32_t       m_ufuLen;
	char       m_ufu[MAX_URL_LEN];

	//int32_t       m_urlLen;
	//char       m_url[MAX_URL_LEN];

	//char       m_username[MAX_USER_SIZE];
	bool       m_strip;
	bool       m_spiderLinks;
	bool       m_forceRespider;
 	// buf filled by the links coming from google, msn, yahoo, etc
	//State2     m_state2[5]; // gb, goog, yahoo, msn, ask
	int32_t       m_numSent;
	int32_t       m_numReceived;
	//int32_t       m_raw;
	//SpiderRequest m_sreq;
};

// only allow up to 1 Msg10's to be in progress at a time
static bool s_inprogress = false;

void doneInjectingWrapper3 ( void *st ) ;

// . returns false if blocked, true otherwise
// . sets g_errno on error
bool sendPageAddUrl ( TcpSocket *sock , HttpRequest *hr ) {
	// . get fields from cgi field of the requested url
	// . get the search query
	int32_t  urlLen = 0;
	char *url = hr->getString ( "urls" , &urlLen , NULL /*default*/);

	// see if they provided a url of a file of urls if they did not
	// provide a url to add directly
	bool isAdmin = g_conf.isCollAdmin ( sock , hr );
	int32_t  ufuLen = 0;
	char *ufu = NULL;
	//if ( isAdmin )
	//	// get the url of a file of urls (ufu)
	//	ufu = hr->getString ( "ufu" , &ufuLen , NULL );

	// can't be too long, that's obnoxious
	if ( urlLen > MAX_URL_LEN || ufuLen > MAX_URL_LEN ) {
		g_errno = EBUFTOOSMALL;
		g_msg = " (error: url too long)";
		return g_httpServer.sendErrorReply(sock,500,"url too long");
	}
	// get the collection
	//int32_t  collLen = 0;
	//char *coll9    = r->getString("c",NULL);//&collLen);
	//if ( ! coll || ! coll[0] ) {
	//	//coll    = g_conf.m_defaultColl;
	//	coll = g_conf.getDefaultColl( r->getHost(), r->getHostLen() );
	//	collLen = gbstrlen(coll);
	//}
	// get collection rec

	CollectionRec *cr = g_collectiondb.getRec ( hr );

	// bitch if no collection rec found
	if ( ! cr ) {
		g_errno = ENOCOLLREC;
		g_msg = " (error: no collection)";
		return g_httpServer.sendErrorReply(sock,500,"no coll rec");
	}
	// . make sure the ip is not banned
	// . we may also have an exclusive list of IPs for private collections
	if ( ! cr->hasSearchPermission ( sock ) ) {
		g_errno = ENOPERM;
		g_msg = " (error: permission denied)";
	       return g_httpServer.sendErrorReply(sock,500,mstrerror(g_errno));
	}


	//
	// if no url, print the main homepage page
	//
	if ( ! url ) {
		SafeBuf sb;
		printAddUrlHomePage ( sb , NULL , hr );
		return g_httpServer.sendDynamicPage(sock, 
						    sb.getBufStart(), 
						    sb.length(),
						    // 120 secs cachetime
						    // don't cache any more
						    // since we have the
						    // login bar at top of page
						    0,//120 ,// cachetime
						    false,// post?
						    "text/html",
						    200,
						    NULL, // cookie
						    "UTF-8",
						    hr);
	}

	//
	// run the ajax script on load to submit the url now 
	//
	int32_t id = hr->getLong("id",0);
	// if we are not being called by the ajax loader, the put the
	// ajax loader script into the html now
	if ( id == 0 ) {
		SafeBuf sb;
		printAddUrlHomePage ( sb , url , hr );
		return g_httpServer.sendDynamicPage ( sock, 
						      sb.getBufStart(), 
						      sb.length(),
						      // don't cache any more
						      // since we have the
						      // login bar at top of 
						      //page
						      0,//3600,// cachetime
						      false,// post?
						      "text/html",
						      200,
						      NULL, // cookie
						      "UTF-8",
						      hr);
	}

	//
	// ok, inject the provided url!!
	//

	//
	// check for errors first
	//

	// if addurl is turned off, just print "disabled" msg
	char *msg = NULL;
	if ( ! g_conf.m_addUrlEnabled ) 
		msg = "Add url is temporarily disabled";
	// can also be turned off in the collection rec
	//if ( ! cr->m_addUrlEnabled    ) 
	//	msg = "Add url is temporarily disabled";
	// or if in read-only mode
	if (   g_conf.m_readOnlyMode  ) 
		msg = "Add url is temporarily disabled";
	// cannot add if another Msg10 from here is still in progress
	if ( s_inprogress ) 
		msg = "Add url is currently busy! Try again in a second.";

	// . send msg back to the ajax request
	// . use cachetime of 3600 so it does not re-inject if you hit the
	//   back button!
	if  ( msg ) {
		SafeBuf sb;
		sb.safePrintf("%s",msg);
		g_httpServer.sendDynamicPage (sock, 
					      sb.getBufStart(), 
					      sb.length(),
					      3600,//-1, // cachetime
					      false,// post?
					      "text/html",
					      200, // http status
					      NULL, // cookie
					      "UTF-8");
		return true;
	}




	// make a new state
	State1i *st1 ;
	try { st1 = new (State1i); }
	catch ( ... ) { 
		g_errno = ENOMEM;
		log("PageAddUrl: new(%i): %s", 
		    (int)sizeof(State1i),mstrerror(g_errno));
	    return g_httpServer.sendErrorReply(sock,500,mstrerror(g_errno)); }
	mnew ( st1 , sizeof(State1i) , "PageAddUrl" );
	// save socket and isAdmin
	st1->m_socket  = sock;
	st1->m_isMasterAdmin = isAdmin;

	/*
	// save the url
	st1->m_url[0] = '\0';
	if ( url ) {
		// normalize and add www. if it needs it
		Url uu;
		uu.set ( url , gbstrlen(url) , true );
		// remove >'s i guess and store in st1->m_url[] buffer
		st1->m_urlLen=cleanInput ( st1->m_url,
					   MAX_URL_LEN, 
					   uu.getUrl(),
					   uu.getUrlLen() );
	}
	*/

	// save the "ufu" (url of file of urls)
	st1->m_ufu[0] = '\0';
	st1->m_ufuLen  = ufuLen;
	gbmemcpy ( st1->m_ufu , ufu , ufuLen );
	st1->m_ufu[ufuLen] = '\0';

	st1->m_doTuringTest = cr->m_doTuringTest;
	st1->m_spiderLinks = true;
	st1->m_strip   = true;

	// save the collection name in the State1i class
	//if ( collLen > MAX_COLL_LEN ) collLen = MAX_COLL_LEN;
	//strncpy ( st1->m_coll , coll , collLen );
	//st1->m_coll [ collLen ] = '\0';

	strcpy ( st1->m_coll , cr->m_coll );

	// assume they answered turing test correctly
	st1->m_goodAnswer = true;

	// get ip of submitter
	//uint32_t h = ipdom ( s->m_ip );
	// . use top 2 bytes now, some isps have large blocks
	// . if this causes problems, then they can do pay for inclusion
	uint32_t h = iptop ( sock->m_ip );
	int32_t codeLen;
	char* code = hr->getString("code", &codeLen);
	if(g_autoBan.hasCode(code, codeLen, sock->m_ip)) {
		int32_t uipLen = 0;
		char* uip = hr->getString("uip",&uipLen);
		int32_t hip = 0;
		//use the uip when we have a raw query to test if 
		//we can submit
		if(uip) {
			hip = atoip(uip, uipLen);
			h = iptop( hip );
		}
	}

	st1->m_strip = hr->getLong("strip",0);
	// . Remember, for cgi, if the box is not checked, then it is not 
	//   reported in the request, so set default return value to 0
	// . support both camel case and all lower-cases
	st1->m_spiderLinks = hr->getLong("spiderLinks",0);
	st1->m_spiderLinks = hr->getLong("spiderlinks",st1->m_spiderLinks);

	// . should we force it into spiderdb even if already in there
	// . use to manually update spider times for a url
	// . however, will not remove old scheduled spider times
	// . mdw: made force on the default
	st1->m_forceRespider = hr->getLong("force",1); // 0);

	int32_t now = getTimeGlobal();
	// . allow 1 submit every 1 hour
	// . restrict by submitter domain ip
	if ( ! st1->m_isMasterAdmin &&
	     ! canSubmit ( h , now , cr->m_maxAddUrlsPerIpDomPerDay ) ) {
		// return error page
		//g_errno = ETOOEARLY;
		SafeBuf sb;
		sb.safePrintf("You breached your add url quota.");
		mdelete ( st1 , sizeof(State1i) , "PageAddUrl" );
		delete (st1);
		// use cachetime of 3600 so it does not re-inject if you hit 
		// the back button!
		g_httpServer.sendDynamicPage (sock, 
					      sb.getBufStart(), 
					      sb.length(),
					      3600,//-1, // cachetime
					      false,// post?
					      "text/html",
					      200, // http status
					      NULL, // cookie
					      "UTF-8");
		return true;
	}

	//st1->m_query = r->getString( "qts", &st1->m_queryLen );

	// check it, if turing test is enabled for this collection
	/*
	if ( ! st1->m_isMasterAdmin && cr->m_doTuringTest && 
	     ! g_turingTest.isHuman(r) )  {
		// log note so we know it didn't make it
		g_msg = " (error: bad answer)";
		//log("PageAddUrl:: addurl failed for %s : bad answer",
		//    iptoa(sock->m_ip));
		st1->m_goodAnswer = false;
		return sendReply ( st1 , true ); // addUrl enabled?
	}
	*/

	Msg7 *msg7 = &st1->m_msg7;
	// set this.
	InjectionRequest *ir = &msg7->m_injectionRequest;

	// default to zero
	memset ( ir , 0 , sizeof(InjectionRequest) );

	// this will fill in GigablastRequest so all the parms we need are set
	//setInjectionRequestFromParms ( sock , hr , cr , ir );

	int32_t  collLen = 0;
	char *coll = hr->getString( "c" , &collLen ,NULL );
	if ( ! coll || ! coll[0] ) {
		coll = g_conf.getDefaultColl( hr->getHost(), hr->getHostLen());
		collLen = gbstrlen(coll);
	}
	ir->m_collnum = g_collectiondb.getCollnum ( coll );

	ir->ptr_url = hr->getString("u",NULL);
	if ( ! ir->ptr_url ) ir->ptr_url = hr->getString("url",NULL);
	if ( ! ir->ptr_url ) ir->ptr_url = hr->getString("urls",NULL);

	if ( ! ir->ptr_url ) {
		g_errno = EBADURL;
		doneInjectingWrapper3  ( st1 );
		return true;
	}

	// include \0 in size
	ir->size_url = gbstrlen(ir->ptr_url)+1;

	// get back a short reply so we can show the status code easily
	ir->m_shortReply = 1;

	ir->m_spiderLinks = st1->m_spiderLinks;

	// this is really an injection, not add url, so make
	// GigablastRequest::m_url point to Gigablast::m_urlsBuf because
	// the PAGE_ADDURLS2 parms in Parms.cpp fill in the m_urlsBuf.
	// HACK!
	//gr->m_url = gr->m_urlsBuf;
	//ir->ptr_url = gr->m_urlsBuf;

	//
	// inject using msg7
	//

	// . pass in the cleaned url
	// . returns false if blocked, true otherwise
	
	if ( ! msg7->sendInjectionRequestToHost ( ir, st1 , 
						  doneInjectingWrapper3 ) ) {
		// there was an error
		log("http: error sending injection request: %s"
		    ,mstrerror(g_errno));
		// we did not block, but had an error
		return true;
	}

	//log("http: injection did not block");

	// some kinda error, g_errno should be set i guess
	//doneInjectingWrapper3  ( st1 );
	// we did not block
	//return true;
	// wait for the reply, this 'blocked'
	return false;

}


void doneInjectingWrapper3 ( void *st ) {
	State1i *st1 = (State1i *)st;
	// allow others to add now
	s_inprogress = false;
	// get the state properly
	//State1i *st1 = (State1i *) state;
	// in order to see what sites are being added log it, then we can
	// more easily remove sites from sitesearch.gigablast.com that are
	// being added but not being searched
	//char *url = st1->m_msg7.m_xd.m_firstUrl.m_url;
	Msg7 *msg7 = &st1->m_msg7;
	InjectionRequest *ir = &msg7->m_injectionRequest;
	char *url = ir->ptr_url;
	log(LOG_INFO,"http: add url %s (%s)",url ,mstrerror(g_errno));
	// extract info from state
	TcpSocket *sock    = st1->m_socket;
	
	//bool       isAdmin = st1->m_isMasterAdmin;
	//char      *url     = NULL;
	//if ( st1->m_urlLen ) url = st1->m_url;
	// re-null it out if just http://
	//bool printUrl = true;
	//if ( st1->m_urlLen == 0 ) printUrl = false;
	//if ( ! st1->m_url       ) printUrl = false;
	//if(st1->m_urlLen==7&&st1->m_url&&!strncasecmp(st1->m_url,"http://",7)
	//	printUrl = false;

	// page is not more than 32k
	char buf[1024*32+MAX_URL_LEN*2];
	SafeBuf sb(buf, 1024*32+MAX_URL_LEN*2);
	
	//char rawbuf[1024*8];
	//SafeBuf rb(rawbuf, 1024*8);	
	//rb.safePrintf("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
	//rb.safePrintf("<status>\n");
	//CollectionRec *cr = g_collectiondb.getRec ( st1->m_coll );
	
	// collection name
	char *coll = st1->m_coll;
	if ( ! coll ) coll = "";

	//char tt [ 128 ];
	//tt[0] = '\0';
	//if ( st1->m_coll[0] != '\0' && ! isAdmin ) 
	//	sprintf ( tt , " for %s", st1->m_coll );


	//
	// what we print here will just be the error msg, because the
	// ajax will fill the text we print here into the div below
	// the add url box
	//

	// if there was an error let them know
	//char msg[MAX_URL_LEN + 1024];
	char *pm = "";
	if ( g_errno ) {
		if ( g_errno == ETOOEARLY ) {
			pm = "Error. 100 urls have "
			"already been submitted by "
			"this IP address for the last 24 hours. "
			"<a href=/addurlerror.html>Explanation</a>.";
			log("addurls: Failed for user at %s: "
			    "quota breeched.", iptoa(sock->m_ip));

			//rb.safePrintf("Error. %"INT32" urls have "
			//	      "already been submitted by "
			//	      "this IP address for the "
			//	      "last 24 hours. ",
			//	      cr->m_maxAddUrlsPerIpDomPerDay);
			sb.safePrintf("%s",pm);
		}
		else {
			sb.safePrintf("Error adding url(s): <b>%s[%i]</b>", 
				      mstrerror(g_errno) , g_errno);
			//pm = msg;
			//rb.safePrintf("Error adding url(s): %s[%i]", 
			//	      mstrerror(g_errno) , g_errno);
			//sb.safePrintf("%s",pm);
		}
	}
	else {
		if ( ! g_conf.m_addUrlEnabled ) {
			pm = "<font color=#ff0000>"
				"Sorry, this feature is temporarily disabled. "
				"Please try again later.</font>";
			if ( url )
				log("addurls: failed for user at %s: "
				    "add url is disabled. "
				    "Enable add url on the "
				    "Master Controls page and "
				    "on the Spider Controls page for "
				    "this collection.", 
				    iptoa(sock->m_ip));

			sb.safePrintf("%s",pm);
			//rb.safePrintf("Sorry, this feature is temporarily "
			//	      "disabled. Please try again later.");
		}
		else if ( s_inprogress ) {
			pm = "Add url busy. Try again later.";
			log("addurls: Failed for user at %s: "
			    "busy adding another.", iptoa(sock->m_ip));
			//rb.safePrintf("Add url busy. Try again later.");
			sb.safePrintf("%s",pm);
		}
		// did they fail the turing test?
		else if ( ! st1->m_goodAnswer ) {
			pm = "<font color=#ff0000>"
				"Oops, you did not enter the 4 large letters "
				"you see below. Please try again.</font>";
			//rb.safePrintf("could not add the url"
			//	      " because the turing test"
			//	      " is enabled.");
			sb.safePrintf("%s",pm);
		}
		else if ( msg7->m_replyIndexCode ) { 
			//st1->m_msg7.m_xd.m_indexCodeValid &&
			//  st1->m_msg7.m_xd.m_indexCode ) {
			//int32_t ic = st1->m_msg7.m_xd.m_indexCode;
			sb.safePrintf("<b>Had error injecting url: %s</b>",
				      mstrerror(msg7->m_replyIndexCode));
		}
		/*
		if ( url && ! st1->m_ufu[0] && url[0] && printUrl ) {
				sprintf ( msg ,"<u>%s</u> added to spider "
					  "queue "
					  "successfully", url );
				//rb.safePrintf("%s added to spider "
				//	      "queue successfully", url );
		}
		else if ( st1->m_ufu[0] ) {
			sprintf ( msg ,"urls in <u>%s</u> "
				  "added to spider queue "
				  "successfully", st1->m_ufu );

			//rb.safePrintf("urls in %s added to spider "
			//	      "queue successfully", url );

		}
		*/
		else {
			//rb.safePrintf("Add the url you want:");
			// avoid hitting browser page cache
			uint32_t rand32 = rand();
			// in the mime to 0 seconds!
			sb.safePrintf("<b>Url successfully added. "
				      "<a href=/search?rand=%"UINT32"&"
				      "c=%s&q=url%%3A",
				      rand32,
				      coll);
			sb.urlEncode(url);
			sb.safePrintf(">Check it</a>"// or "
				      //"<a href=http://www.gigablast."
				      //"com/seo?u=");
				      //sb.urlEncode(url);
				      //sb.safePrintf(">SEO it</a>"
				      "."
				      "</b>");
		}
			
		//pm = msg;
		//url = "http://";
		//else
		//	pm = "Don't forget to <a href=/gigaboost.html>"
		//		"Gigaboost</a> your URL.";
	}

	// store it
	sb.safePrintf("<b>%s</b>",pm );

	// clear g_errno, if any, so our reply send goes through
	g_errno = 0;


	// nuke state
	mdelete ( st1 , sizeof(State1i) , "PageAddUrl" );
	delete (st1);

	// this reply should be loaded from the ajax loader so use a cache
	// time of 1 hour so it does not re-inject the url if you hit the
	// back button
	g_httpServer.sendDynamicPage (sock, 
				      sb.getBufStart(), 
				      sb.length(),
				      3600, // cachetime
				      false,// post?
				      "text/html",
				      200, // http status
				      NULL, // cookie
				      "UTF-8");
}


// we get like 100k submissions a day!!!
static HashTable s_htable;
static bool      s_init = false;
static int32_t      s_lastTime = 0;
bool canSubmit ( uint32_t h , int32_t now , int32_t maxAddUrlsPerIpDomPerDay ) {
	// . sometimes no limit
	// . 0 means no limit because if they don't want any submission they
	//   can just turn off add url and we want to avoid excess 
	//   troubleshooting for why a url can't be added
	if ( maxAddUrlsPerIpDomPerDay <= 0 ) return true;
	// init the table
	if ( ! s_init ) {
		s_htable.set ( 50000 );
		s_init = true;
	}
	// clean out table every 24 hours
	if ( now - s_lastTime > 24*60*60 ) {
		s_lastTime = now;
		s_htable.clear();
	}
	// . if table almost full clean out ALL slots
	// . TODO: just clean out oldest slots
	if ( s_htable.getNumSlotsUsed() > 47000 ) s_htable.clear ();
	// . how many times has this IP domain submitted?
	// . allow 10 times per day
	int32_t n = s_htable.getValue ( h );
	// if over 24hr limit then bail
	if ( n >= maxAddUrlsPerIpDomPerDay ) return false;
	// otherwise, inc it
	n++;
	// add to table, will replace old values
	s_htable.addKey ( h , n );
	return true;
}


void resetPageAddUrl ( ) {
	s_htable.reset();
}

/*
bool sendPageAdvanced ( TcpSocket *sock , HttpRequest *hr ) {

	SafeBuf sb;

	CollectionRec *cr = g_collectiondb.getRec ( hr );

	printFrontPageShell ( &sb , "advanced" , cr , true );

	sb.safePrintf("<br><br>\n");
	sb.safePrintf("<br><br><br>\n");

	// submit to https now
	sb.safePrintf("<form method=GET "
		      "action=/search name=f>\n" );

	char *coll = "";
	if ( cr ) coll = cr->m_coll;
	if ( cr )
		sb.safePrintf("<input type=hidden name=c value=\"%s\">",
			      cr->m_coll);


	sb.safePrintf(
	"<script type=text/javascript>"
	"<!--"
	"function x(){document.f.q.focus();}"
	"// -->"
	"</script>"
	"</head>"
	""

	"<body onload=x()>"

	//"<form method=get action=/search>"

	"	<table width=605 border=0 align=center cellpadding=5 cellspacing=3>"
	"		<tbody>"
	"			<tr align=left valign=middle>"
	"			<th colspan=3>Search for...</th>"
	"			</tr>"
	"			<tr align=left valign=middle>"
	"				<td><strong>all</strong> of these words</td>"
	"				<td><input type=text name=plus size=40 />"

	"</td><td>"

			"<div onclick=document.f.submit(); "

			" onmouseover=\""
			"this.style.backgroundColor='lightgreen';"
			"this.style.color='black';\""
			" onmouseout=\""
			"this.style.backgroundColor='green';"
			"this.style.color='white';\" "

			"style=border-radius:28px;"
			"cursor:pointer;"
			"cursor:hand;"
			"border-color:white;"
			"border-style:solid;"
			"border-width:3px;"
			"padding:12px;"
			"width:20px;"
			"height:20px;"
			"display:inline-block;"
			"background-color:green;color:white;>"
			"<b style=margin-left:-5px;font-size:18px;"
			">GO</b>"
			"</div>"
	"</td>"
	"			</tr>"
	"			<tr align=left valign=middle>"
	"				<td>this <strong>exact phrase</strong></td>"
	"				<td colspan=2><input type=text name=quote1 size=40 /></td>"
	"			</tr>"
	"			<tr align=left valign=middle>"
	"				<td>and this <strong>exact phrase</strong></td>"
	"				<td colspan=2><input type=text name=quote2 size=40 /></td>"
	"			</tr>"
	"			<tr align=left valign=middle>"
	"				<td><strong>any</strong> of these words</td>"
	"				<td colspan=2><input type=text name=q size=40 /></td>"
	"			</tr>"
	"			<tr align=left valign=middle>"
	"				<td><strong>none</strong> of these words</td>"
	"				<td colspan=2><input type=text name=minus size=40 /></td>"
	"			</tr>"
	""
	"			<tr align=left valign=middle>"
	"				<td>In this language:"
	"				</td>"
	"				<td colspan=2>"
	"				<select name=gblang>"
	"				<option value=0>Any</option>"
	"				<option value=1>English</option>"
	"<option value=2>French</option>	"
	"<option value=3>Spanish</option>"
	"<option value=4>Russian</option>"
	"<option value=5>Turkish</option>"
	"<option value=6>Japanese</option>"
	"<option value=7>ChineseTrad</option>"
	"<option value=8>ChineseSimp</option>"
	"<option value=9>Korean</option>"
	"<option value=10>German</option>"
	"<option value=11>Dutch</option>"
	"<option value=12>Italian</option>"
	"<option value=13>Finnish</option>"
	"<option value=14>Swedish</option>"
	"<option value=15>Norwegian</option>"
	"<option value=16>Portuguese</option>"
	"<option value=17>Vietnamese</option>"
	"<option value=18>Arabic</option>"
	"<option value=19>Hebrew</option>"
	"<option value=20>Indonesian</option>"
	"<option value=21>Greek</option>"
	"<option value=22>Thai</option>"
	"<option value=23>Hindi</option>"
	"<option value=24>Bengala</option>"
	"<option value=25>Polish</option>"
	"<option value=26>Tagalog</option>"
	"				</select>"
	"				</td>"
	"			</tr>"
	""
	""
	"			<tr align=left valign=middle>"
	"				<td>Restrict to this URL</td>"
	"				<td colspan=2><input type=text name=url size=40 /></td>"
	"			</tr>"
	"			<tr align=left valign=middle>"
	"				<td>Pages that link to this URL</td>"
	"				<td colspan=2><input type=text name=link size=40 /></td>"
	"			</tr>"
	"			<tr align=left valign=middle>"
	"				<td>Site Clustering</td>"
	"				<td colspan=2><input type=radio name=sc value=1 checked=checked />yes&nbsp;&nbsp;&nbsp;<input type=radio name=sc value=0 />no</td>"
	"			</tr>"
	"			<tr align=left valign=middle>"
	"				<td>Number of summary excerpts</td>"
	"				<td colspan=2><input type=radio name=ns value=0 />0&nbsp;&nbsp;&nbsp;<input type=radio name=ns value=1 />1&nbsp;&nbsp;&nbsp;<input type=radio name=ns value=2 />2&nbsp;&nbsp;&nbsp;<input type=radio name=ns value=3 checked=checked />3&nbsp;&nbsp;&nbsp;<input type=radio name=ns value=4 />4&nbsp;&nbsp;&nbsp;<input type=radio name=ns value=5 />5</td>"
	"			</tr>"
	"			<tr align=left valign=middle>"
	"				<td>Results per Page</td>"
	"				<td colspan=2><input type=radio name=n value=10 checked=checked />10&nbsp;&nbsp;<input type=radio name=n value=20 />20&nbsp;&nbsp;<input type=radio name=n value=30 />30&nbsp;&nbsp;<input type=radio name=n value=40 />40&nbsp;&nbsp;<input type=radio name=n value=50 />50&nbsp;&nbsp;<input type=radio name=n value=100 />100</td>"
	"			</tr>"
	"			<tr align=left valign=middle>"
	"				<td>Restrict to these Sites</td>"
	"				<td colspan=2><textarea rows=10 cols=40 name=sites></textarea></td>"
	"			</tr>"
	"	  </tbody>"
	"	</table>"
		      );



	sb.safePrintf("</form>\n");
	sb.safePrintf("<br>\n");
	sb.safePrintf("\n");
	sb.safePrintf("<br><br>\n");

	printNav ( sb , hr );

	g_httpServer.sendDynamicPage (sock, 
				      sb.getBufStart(), 
				      sb.length(),
				      3600, // cachetime
				      false,// post?
				      "text/html",
				      200, // http status
				      NULL, // cookie
				      "UTF-8");

	return true;
}
*/

bool sendPageHelp ( TcpSocket *sock , HttpRequest *hr ) {

	SafeBuf sb;

	CollectionRec *cr = g_collectiondb.getRec ( hr );

	printFrontPageShell ( &sb , "syntax" , cr , true );

	sb.safePrintf("<br><br>\n");
	sb.safePrintf("<br><br><br>\n");

	// submit to https now
	//sb.safePrintf("<form method=GET "
	//	      "action=/addurl name=f>\n" );

	// char *coll = "";
	// if ( cr ) coll = cr->m_coll;
	// if ( cr )
	// 	sb.safePrintf("<input type=hidden name=c value=\"%s\">",
	// 		      cr->m_coll);


	char *qc = "demo";
	char *host = "http://www.gigablast.com";
	// for debug make it local on laptop
	host = "";

	sb.safePrintf(
	"<br>"
	"<table width=650px cellpadding=5 cellspacing=0 border=0>"
	""

	// yellow/gold bar
	"<tr>"
	"<td colspan=2 bgcolor=#%s>" // f3c714>"
	"<b>"
	"Basic Query Syntax"
	"</b>"
	"</td>"
	"</tr>\n"

	"<tr bgcolor=#0340fd>"
	""
	"<th><font color=33dcff>Example Query</font></th>"
	"<th><font color=33dcff>Description</font></th>"
	"</tr>"
	"<tr> "
	"<td><a href=%s/search?c=%s&q=cat+dog>cat dog</a></td>"
	"            <td>Search results have the word <em>cat</em> and the word <em>dog</em> "
	"              in them. They could also have <i>cats</i> and <i>dogs</i>.</td>"
	"          </tr>"
	""
	""
	"          <tr bgcolor=#E1FFFF> "
	"            <td><a href=%s/search?c=%s&q=%%2Bcat>+cat</a></td>"
	"            <td>Search results have the word <em>cat</em> in them. If the search results has the word <i>cats</i> then it will not be included. The plus sign indicates an exact match and not to use synonyms, hypernyms or hyponyms or any other form of the word.</td>"
	"          </tr>"
	""
	""
	"          <tr> "
	"            <td height=10><a href=%s/search?c=%s&q=mp3+%%22take+five%%22>mp3&nbsp;\"take&nbsp;five\"</a></td>"
	"            <td>Search results have the word <em>mp3</em> and the exact phrase <em>take "
	"              five</em> in them.</td>"
	"          </tr>"
	"          <tr bgcolor=#E1FFFF> "
	"            <td><a href=%s/search?c=%s&q=%%22john+smith%%22+-%%22bob+dole%%22>\"john&nbsp;smith\"&nbsp;-\"bob&nbsp;dole\"</a></td>"
	"            <td>Search results have the phrase <em>john smith</em> but NOT the "
	"              phrase <em>bob dole</em> in them.</td>"
	"          </tr>"
	"          <tr> "
	"            <td><a href=%s/search?c=%s&q=bmx+-game>bmx&nbsp;-game</a></td>"
	"            <td>Search results have the word <em>bmx</em> but not <em>game</em>.</td>"
	"          </tr>"
	// "          <tr> "
	// "            <td><a href=/search?q=inurl%%3Aedu+title%%3Auniversity><b>inurl:</b></a><a href=/search?q=inurl%%3Aedu+title%%3Auniversity>edu <b>title:</b>university</a></td>"
	// "            <td>Search results have <em>university</em> in their title and <em>edu</em> "
	// "              in their url.</td>"
	// "          </tr>"
	// "          <tr bgcolor=#E1FFFF> "
	// "            <td><a href=/search?q=site%%3Awww.ibm.com+%%22big+blue%%22><b>site:</b></a><a href=/search?q=site%%3Awww.ibm.com+%%22big+blue%%22>www.ibm.com&nbsp;\"big&nbsp;blue\"</a></td>"
	// "            <td>Search results are from the site <em>www.ibm.com</em> and have the phrase "
	// "              <em>big blue</em> in them.</td>"
	// "          </tr>"
	// "          <tr> "
	// "            <td><a href=/search?q=url%%3Awww.yahoo.com><b>url:</b></a><a href=/search?q=url%%3Awww.yahoo.com&n=10>www.yahoo.com</a></td>"
	// "            <td>Search result is the single URL www.yahoo.com, if it is indexed.</td>"
	// "          </tr>"

	// "          <tr bgcolor=#E1FFFF> "
	// "            <td><nobr><a href=/search?q=title%%3A%%22the+news%%22+-%%22weather+report%%22><b>title:</b>\"the "
	// "              news\" -\"weather report\"</a></nobr></td>"
	// "            <td>Search results have the phrase <em>the news</em> in their title, "
	// "              and do NOT have the phrase <em>weather report</em> anywhere in their "
	// "              content.</td>"
	// "          </tr>"
	// "          <tr> "
	// "            <td><a href=/search?q=ip%%3A216.32.120+cars><b>ip:</b></a><a href=/search?q=ip%%3A216.32.120>216.32.120</a></td>"
	// "            <td>Search results are from the the ip 216.32.120.*.</td>"
	// "          </tr>"
	// ""
	// "          <tr bgcolor=#E1FFFF> "
	// "            <td><a href=/search?q=type%%3Apdf+nutrition><b>type:</b>pdf nutrition</a></td>"
	// "            <td>Search results are PDF (Portable Document Format) documents that "
	// "              contain the word <em>nutrition</em>.</td>"
	// "          </tr>"
	// "          <tr> "
	// "            <td><a href=/search?q=type%%3Adoc><b>type:</b>doc</a></td>"
	// "            <td>Search results are Microsoft Word documents.</td>"
	// "          </tr>"
	// "          <tr bgcolor=#E1FFFF> "
	// "            <td><a href=/search?q=type%%3Axls><b>type:</b>xls</a></td>"
	// "            <td>Search results are Microsoft Excel documents.</td>"
	// "          </tr>"
	// "          <tr> "
	// "            <td><a href=/search?q=type%%3Appt><b>type:</b>ppt</a></td>"
	// "            <td>Search results are Microsoft Power Point documents.</td>"
	// "          </tr>"
	// "          <tr bgcolor=#E1FFFF> "
	// "            <td><a href=/search?q=type%%3Aps><b>type:</b>ps</a></td>"
	// "            <td>Search results are Postscript documents.</td>"
	// "          </tr>"
	// "          <tr> "
	// "            <td><a href=/search?q=type%%3Atext><b>type:</b>text</a></td>"
	// "            <td>Search results are plain text documents.</td>"
	// "          </tr>"
	// "          <tr bgcolor=#E1FFFF> "
	// "            <td><a href=/search?q=filetype%%3Apdf><b>filetype:</b>pdf</a></td>"
	// "            <td>Search results are PDF documents.</td>"
	// "          </tr>"
	// ""
	// ""
	// "          <tr bgcolor=#E1FFFF> "
	// "            <td><a href=/search?q=link%%3Awww.yahoo.com><b>link:</b>www.yahoo.com</a></td>"
	// "            <td>All the pages that link to www.yahoo.com.</td>"
	// "          </tr>"
	// ""
	// "          <tr bgcolor=#E1FFFF> "
	// "            <td><a href=/search?q=sitelink%%3Awww.yahoo.com><b>sitelink:</b>www.yahoo.com</a></td>"
	// "            <td>All the pages that link to any page on www.yahoo.com.</td>"
	// "          </tr>"
	// ""
	// "          <tr bgcolor=#E1FFFF> "
	// "            <td><a href=/search?q=ext%%3Atxt><b>ext:</b>txt</a></td>"
	// "            <td>All the pages whose url ends in the .txt extension.</td>"
	// "          </tr>"
	// ""
	// ""
	, GOLD

	, host
	, qc
	, host
	, qc
	, host
	, qc
	, host
	, qc
	, host
	, qc
		      );


	sb.safePrintf(
		      // spacer
		      //"<tr><td><br></td><td></td></tr>"

		      //"<tr bgcolor=#0340fd>"
		      // "<td><font color=33dcff><b>Special Query</b>"
		      // "</font></td>"
		      //"<td><font color=33dcff><b>Description</b></font></td>"
		      // "</tr>"
		      "<tr bgcolor=#E1FFFF>"
		      "<td><a href=%s/search?c=%s&q=cat|dog>cat | dog</a>"
		      "</td><td>"
		      "Match documents that have cat and dog in them, but "
		      "do not allow cat to affect the ranking score, only "
		      "dog. This is called a <i>query refinement</i>."
		      "</td></tr>\n"

		      "<tr bgcolor=#ffFFFF>"
		      "<td><a href=%s/search?c=%s&q=document.title:paper>"
		      "document.title:paper</a></td><td>"
		      "That query will match a JSON document like "
		      "<i>"
		      "{ \"document\":{\"title\":\"This is a good paper.\" "
		      "}}</i> or, alternatively, an XML document like <i>"

		      , host
		      , qc
		      , host
		      , qc

		      );
	sb.htmlEncode("<document><title>This is a good paper"
		      "</title></document>" );
	sb.safePrintf("</i></td></tr>\n");


	char *bg1 = "#E1FFFF";
	char *bg2 = "#ffffff";
	char *bgcolor = bg1;

	// table of the query keywords
	int32_t n = getNumFieldCodes();
	for ( int32_t i = 0 ; i < n ; i++ ) {
		// get field #i
		QueryField *f = &g_fields[i];

		if ( g_fields[i].m_flag & QTF_HIDE ) continue;
		

		// new table?
		if ( g_fields[i].m_flag & QTF_BEGINNEWTABLE ) {
			sb.safePrintf("</table>"
				      "<br>"
				      "<br>"
				      "<br>"
				      "<table width=650px "
				      "cellpadding=5 cellspacing=0 border=0>"
				      // yellow/gold bar
				      "<tr>"
				      "<td colspan=2 bgcolor=#%s>"//f3c714>"
				      "<b>"
				      "%s"
				      "</b>"
				      "</td>"
				      "</tr>\n"
				      "<tr bgcolor=#0340fd>"
				      "<th><font color=33dcff>"
				      "Example Query</font></th>"
				      "<th><font color=33dcff>"
				      "Description</font></th>"
				      "</tr>\n"
				      , GOLD
				      , g_fields[i].m_title
				      );
		}

		// print it out
		char *d = f->desc;
		// fix table internal cell bordering
		if ( ! d || d[0] == '\0' ) d = "&nbsp;";
		sb.safePrintf("<tr bgcolor=%s>"
			      "<td><nobr><a href=\"%s/search?c=%s&q="
			      , bgcolor
			      , host
			      , qc
			      );
		sb.urlEncode ( f->example );
		sb.safePrintf("\">");
		sb.safePrintf("%s</a></nobr></td>"
			      "<td>%s</td></tr>\n",
			      f->example,
			      d);

		if ( bgcolor == bg1 ) bgcolor = bg2;
		else                  bgcolor = bg1;
	}




	sb.safePrintf(
	// "          <tr> "
	// "            <td style=padding-bottom:12px;>&nbsp;</td>"
	// "            <td style=padding-bottom:12px;>&nbsp;</td>"
	// "          </tr>"
	// ""

		      "</table>"
		      
		      "<br><br><br>"

		      "<table width=650px "
		      "cellpadding=5 cellspacing=0 border=0>"

	// yellow/gold bar
	"<tr>"
		      "<td colspan=2 bgcolor=#%s>" // f3c714>"
	"<b>"
	"Boolean Queries"
	"</b>"
	"</td>"
	"</tr>\n"


	"<tr bgcolor=#0340fd>"
	""
	"            <th><font color=33dcff>Example Query</font></th>"
	"            <th><font color=33dcff>Description</font></th>"
	""
	"          </tr>"
	""
	"          <tr> "
	"            <td colspan=2 bgcolor=#FFFFCC><center>"
	"                Note: boolean operators must be in UPPER CASE. "
	"              </td>"
	"          </tr>"
	"          <tr> "
	"            <td><a href=%s/search?c=%s&q=cat+AND+dog>cat&nbsp;AND&nbsp;dog</a></td>"
	"            <td>Search results have the word <em>cat</em> AND the word <em>dog</em> "
	"              in them.</td>"
	"          </tr>"
	"          <tr bgcolor=#E1FFFF> "
	"            <td><a href=%s/search?c=%s&q=cat+OR+dog>cat&nbsp;OR&nbsp;dog</a></td>"
	"            <td>Search results have the word <em>cat</em> OR the word <em>dog</em> "
	"              in them, but preference is given to results that have both words.</td>"
	"          </tr>"
	"          <tr> "
	"            <td><a href=%s/search?c=%s&q=cat+dog+OR+pig>cat&nbsp;dog&nbsp;OR&nbsp;pig</a></td>"
	"            <td>Search results have the two words <em>cat</em> and <em>dog</em> "
	"              OR search results have the word <em>pig</em>, but preference is "
	"              given to results that have all three words. This illustrates how "
	"              the individual words of one operand are all required for that operand "
	"              to be true.</td>"
	"          </tr>"
	"          <tr bgcolor=#E1FFFF> "
	"            <td><a href=%s/search?c=%s&q=%%22cat+dog%%22+OR+pig>\"cat&nbsp;dog\"&nbsp;OR&nbsp;pig</a></td>"
	"            <td>Search results have the phrase <em>\"cat dog\"</em> in them OR they "
	"              have the word <em>pig</em>, but preference is given to results that "
	"              have both.</td>"
	"          </tr>"
	"          <tr> "
	"            <td><a href=%s/search?c=%s&q=title%%3A%%22cat+dog%%22+OR+pig>title</a><a href=%s/search?c=%s&q=title%%3A%%22cat+dog%%22+OR+pig>:\"cat "
	"              dog\" OR pig</a></td>"
	"            <td>Search results have the phrase <em>\"cat dog\"</em> in their title "
	"              OR they have the word <em>pig</em>, but preference is given to results "
	"              that have both.</td>"
	"          </tr>"
	"          <tr bgcolor=#E1FFFF> "
	"            <td><a href=%s/search?c=%s&q=cat+OR+dog+OR+pig>cat&nbsp;OR&nbsp;dog&nbsp;OR&nbsp;pig</a></td>"
	"            <td>Search results need only have one word, <em>cat</em> or <em>dog</em> "
	"              or <em>pig</em>, but preference is given to results that have the "
	"              most of the words.</td>"
	"          </tr>"
	"          <tr> "
	"            <td><a href=%s/search?c=%s&q=cat+OR+dog+AND+pig>cat&nbsp;OR&nbsp;dog&nbsp;AND&nbsp;pig</a></td>"
	"            <td>Search results have <em>dog</em> and <em>pig</em>, but they may "
	"              or may not have <em>cat</em>. Preference is given to results that "
	"              have all three. To evaluate expressions with more than two operands, "
	"              as in this case where we have three, you can divide the expression "
	"              up into sub-expressions that consist of only one operator each. "
	"              In this case we would have the following two sub-expressions: <em>cat "
	"              OR dog</em> and <em>dog AND pig</em>. Then, for the original expression "
	"              to be true, at least one of the sub-expressions that have an OR "
	"              operator must be true, and, in addition, all of the sub-expressions "
	"              that have AND operators must be true. Using this logic you can evaluate "
	"              expressions with more than one boolean operator.</td>"
	"          </tr>"
	"          <tr bgcolor=#E1FFFF> "
	"            <td><a href=%s/search?c=%s&q=cat+AND+NOT+dog>cat&nbsp;AND&nbsp;NOT&nbsp;dog</a></td>"
	"            <td>Search results have <em>cat</em> but do not have <em>dog</em>.</td>"
	"          </tr>"
	"          <tr> "
	"            <td><a href=%s/search?c=%s&q=cat+AND+NOT+%%28dog+OR+pig%%29>cat&nbsp;AND&nbsp;NOT&nbsp;(dog&nbsp;OR&nbsp;pig)</a></td>"
	"            <td>Search results have <em>cat</em> but do not have <em>dog</em> "
	"              and do not have <em>pig</em>. When evaluating a boolean expression "
	"              that contains ()'s you can evaluate the sub-expression in the ()'s "
	"              first. So if a document has <em>dog</em> or it has <em>pig</em> "
	"              or it has both, then the expression, <em>(dog OR pig)</em> would "
	"              be true. So you could, in this case, substitute <em>true</em> for "
	"              that expression to get the following: <em>cat AND NOT (true) = cat "
	"              AND false = false</em>. Does anyone actually read this far?</td>"
	"          </tr>"
	"          <tr bgcolor=#E1FFFF> "
	"            <td><a href=%s/search?c=%s&q=%%28cat+OR+dog%%29+AND+NOT+%%28cat+AND+dog%%29>(cat&nbsp;OR&nbsp;dog)&nbsp;AND&nbsp;NOT&nbsp;(cat&nbsp;AND&nbsp;dog)</a></td>"
	"            <td>Search results have <em>cat</em> or <em>dog</em> but not both.</td>"
	"          </tr>"
	"          <tr> "
	"            <td>left-operand&nbsp;&nbsp;OPERATOR&nbsp;&nbsp;right-operand</td>"
	"            <td>This is the general format of a boolean expression. The possible "
	"              operators are: OR and AND. The operands can themselves be boolean "
	"              expressions and can be optionally enclosed in parentheses. A NOT "
	"              operator can optionally preceed the left or the right operand.</td>"
	"          </tr>"
	""
	//"        </table>"
	""
	""
	""
	//"</td></tr>"
	//"</table>"
	//"<br>"
		      , GOLD
		      , host
		      , qc
		      , host
		      , qc
		      , host
		      , qc
		      , host
		      , qc
		      , host
		      , qc
		      , host
		      , qc
		      , host
		      , qc
		      , host
		      , qc
		      , host
		      , qc
		      , host
		      , qc
		      , host
		      , qc
		      );



	//sb.safePrintf("<tr><td></td><td></td></tr>\n");
	//sb.safePrintf("<tr><td></td><td></td></tr>\n");
	//sb.safePrintf("<tr><td></td><td></td></tr>\n");
	//sb.safePrintf("<tr><td></td><td></td></tr>\n");

	
	sb.safePrintf("</table>");


	//sb.safePrintf("</form>\n");
	sb.safePrintf("<br>\n");
	sb.safePrintf("\n");
	sb.safePrintf("<br><br>\n");

	printNav ( sb , hr );

	g_httpServer.sendDynamicPage (sock, 
				      sb.getBufStart(), 
				      sb.length(),
				      3600, // cachetime
				      false,// post?
				      "text/html",
				      200, // http status
				      NULL, // cookie
				      "UTF-8");

	return true;
}

