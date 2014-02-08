#include "SafeBuf.h"
#include "HttpRequest.h"
#include "SearchInput.h"
#include "Pages.h"
#include "Parms.h"

bool printSiteListBox ( SafeBuf *sb , HttpRequest *hr ) ;

//
// main > Basic > Settings
//
bool sendPageBasicSettings ( TcpSocket *socket , HttpRequest *hr ) {

	char  buf [ 128000 ];
	SafeBuf sb(buf,128000);

	char *fs = hr->getString("format",NULL,NULL);
	char fmt = FORMAT_HTML;
	if ( fs && strcmp(fs,"html") == 0 ) fmt = FORMAT_HTML;
	if ( fs && strcmp(fs,"json") == 0 ) fmt = FORMAT_JSON;
	if ( fs && strcmp(fs,"xml") == 0 ) fmt = FORMAT_XML;


	// print standard header 
	if ( fmt == FORMAT_HTML )
		g_pages.printAdminTop ( &sb , socket , hr );


	CollectionRec *cr = getCollRecFromHttpRequest ( hr );
	if ( ! cr ) {
		g_httpServer.sendErrorReply(socket,500,"invalid collection");
		return true;
	}

	sb.safePrintf("<form method=POST submit=/basic/settings>\n");
	
	// print pause or resume button
	if ( cr->m_spideringEnabled )
		sb.safePrintf("<input type=submit "
			      "style=\""
			      "font:Helvetica Neue,Helvetica Arial;"
			      "\" "
			      "text=\"Pause Spidering\" "
			      "name=pause value=1>");
	else
		sb.safePrintf("<input type=submit "
			      "style=\""
			      "font:Helvetica Neue,Helvetica Arial;"
			      "\" "
			      "text=\"Resume Spidering\" "
			      "name=pause value=0>");

	sb.safePrintf(" &nbsp; &nbsp; ");

	// the restart button
	sb.safePrintf("<input type=submit text=\"Restart Collection\" "
		      "name=restart value=1 title=\"Reset "
		      "the current collection's index and start spidering "
		      "over, but keep all the settings and "
		      "the site list below.\">");


	sb.safePrintf("<br><br>");

	// also used in the advanced controls under the "add url" tab i guess
	printSiteListBox ( &sb , hr );

	if ( fmt == FORMAT_HTML ) sb.safePrintf ( "<br><br>\n" );

	if ( fmt != FORMAT_JSON )
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



bool printSiteListBox ( SafeBuf *sb , HttpRequest *hr ) {

	CollectionRec *cr = getCollectionRec ( hr );
	if ( ! cr ) return true;

	char *submittedSiteList = hr->getString("sitelist" );

	// we do not automatically set this parm so that we can verify it
	// before setting cr->m_siteListBuf
	bool valid = true;
	SafeBuf validMsg;
	if ( submittedSiteList ) 
		valid = validateSiteList (submittedSiteList,&validMsg);


	// if it is a valid list of sites... broadcast it to all hosts
	// so they can update cr->m_siteList with it. when they get it
	// they will have to update their siteListTable hashtable so which
	// we use to quickly determine if we should spider a url or not
	// in Spider.cpp
	if ( valid && submittedSiteList &&
	     // if it was too big this might say oom i guess
	     ! g_parms.broadcastParm( submittedSiteList , "sitelist" ) ) {
		// tell the browser why we failed
		validMsg.safePrintf("Error distributing site list: %s",
				    mstrerror(g_errno));
		valid = false;
	}
	


	// print if submitted site list is valid or not
	if ( ! valid )
		sb.safePrintf("<br><font color=red><b>"
			      "%s"
			      "</b></font>"
			      "<br>"
			      , validMsg.getBufStart() );
	

	// it is a safebuf parm
	char *siteList = cr->m_siteListBuf.getBufStart();

	SafeBuf msgBuf;
	char *status = "";
	long max = 100000;
	if ( cr->m_numSiteEntries > max ) {
		msgBuf.safePrintf( "<font color=red><b>"
				   "There are %li site entries, too many to "
				   "display on this web page. Please use the "
				   "file upload feature only for now."
				   "</b></font>"
				   , max );
		status = " disabled";
	}

	char *msg2 = msgBuf.getBufStart();
	if ( ! msg2 ) msg2 = "";

	// now list of sites to include, or exclude
	sb->safePrintf ( "List of sites to spider, one per line:"
			 "<br>"
			 "%s"
			 "<br>"
			 "<textarea cols=80 rows=40%s>"
			 , msg2
			 , status
			 );

	// print sites
	sb->safeMemcpy ( &cr->m_siteListBuf );

	sb->safePrintf("</textarea>\n");


	sb->safePrintf("<br>"
		       "<br>"
		       //"Alternatively you can edit the local "
		       //"file %s/coll.%s.%li/sitelist.txt and "
		       //"then click this link: <a>reload file</a>. "
		       //"Or you can <a>upload a file</a> "
		       "Alternatively, you can "
		       "<input "
		       "size=20 "
		       "type=file "
		       "name=\"Upload a File\"> of "
		       "urls "
		       "to REPLACE all the urls in here now. If there "
		       "is an error with your submission then "
		       "Gigablast will tell you and not "
		       "perform the replacement. "

		       "<br><br>"

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

		       );
			      
	sb->safePrintf("<br><br>");

	// example table
	sb->safePrintf("<table>"
		      "<tr><td colspan=2><center>Examples"
		      "</center></td></tr>" );

	sb->safePrintf(

		      "<tr>"
		      "<td>*</td>"
		      "<td>Spider all urls encountered. If you just enter "
		      "this by itself, then Gigablast will initiate spidering "
		      "automatically at dmoz.org, an internet "
		      "directory of good sites.</td>"
		      "</tr>"

		      "<tr>"
		      "<td>goodstuff.com</td>"
		      "<td>"
		      "Spider urls on goodstuff.com and on "
		      "any subdomain of goodstuff.com"
		      "</td>"
		      "</tr>"

		      "<tr>"
		      "<td>http://goodstuff.com</td>"
		      "<td>"
		      "Only spider urls beginning with http://goodstuff.com/ "
		      "</td>"
		      "</tr>"

		      "<tr>"
		      "<td>https://goodstuff.com</td>"
		      "<td>"
		      "Only spider urls beginning with https://goodstuff.com/ "
		      "</td>"
		      "</tr>"

		      "<tr>"
		      "<td>http://*.goodstuff.com</td>"
		      "<td>"
		      "Only spider urls from a subdomain of goodstuff.com "
		      "and only using the http, not https, protocol."
		      "</td>"
		      "</tr>"

		      "<tr>"
		      "<td>http://xyz.goodstuff.com/$</td>"
		      "<td>"
		      "Only spider the single url http://xyz.goodstuff.com/"
		      "</td>"
		      "</tr>"

		      "<tr>"
		      "<td>goodstuff.com/mydir/</td>"
		      "<td>"
		      "Spider urls on any subdomain of goodstuff.com AND "
		      "in the /mydir/ directory or subdirectory thereof."
		      "</td>"
		      "</tr>"

		      /*
		      "<tr>"
		      "<td>goodstuff.com/mydir/ *boots*</td>"
		      "<td>"
		      "Spider urls on any subdomain of goodstuff.com AND "
		      "in the /mydir/ directory or subdirectory thereof "
		      "AND with the word boots somewhere in the url."
		      "</td>"
		      "</tr>"

		      "<tr>"
		      "<td>goodstuff.com/mydir/ *boots$</td>"
		      "<td>"
		      "Spider urls on any subdomain of goodstuff.com AND "
		      "in the /mydir/ directory or subdirectory thereof "
		      "AND ENDING in the word boots."
		      "</td>"
		      "</tr>"
		      */

		      "<tr>"
		      "<td>file://C/mydir/mysubdir/"
		      "<td>"
		      "Spider all local files in the specified directory."
		      "</td>"
		      "</tr>"

		      "<tr>"
		      "<td>-badstuff.com</td>"
		      "<td>Exclude all pages from badstuff.com</td>"
		      "</tr>"

		      "<tr>"
		      "<td>mytag goodstuff.com</td>"
		      "<td>"
		      "Advanced users only. "
		      "Tag all urls from goodstuff.com with <i>mytag</i> "
		      "which can be used like <i>tag:mytag</i> in the "
		      "<a href=/scheduler>spider scheduler</a> for more "
		      "precise spidering control over url subsets."
		      "</td>"
		      "</tr>"

		      "</table>"
		      );
	
}
