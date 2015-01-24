// http://www.propeciauk.co.uk/links.htm
// http://www.hendersonvillehomepro.com/FavoriteLinks/Default.aspx
// http://www.viacreme-viacream-viagra.com/health/pharmacies.htm
// are the same description for viagrapunch.com. why did they not cancel?

#include "linkspam.h"
#include "Url.h"
#include "Linkdb.h"
//#include "TitleRec.h"
#include "Unicode.h"
#include "matches2.h"
#include "Categories.h"

bool isLinkChain ( Xml *xml , Url *linker , Url *linkee , int32_t linkNode ,
		   char **note ) ;

// . here's some additional things to mark it as a log page, but these
//   depend on the content of the page, not the url itself.
// . fields: string, stringLen, id, section?
// . section is "1" if the substring identifies the start of a comment
//   section, so that any links above that identifier should be 
//   consider good, and any below, should be considered bad links.
//   Otherwise, if section is 0, if the match occurs anywhere on the
//   page then all links on the page should be considered bad.
static Needle s_needles1[] = {
	{"open.thumbshots.org"          , 0 , 0 , 0 , 0 , NULL , 0 , NULL } ,
	//{"google-ad"                    , 0 , 0 , 0 , 0 , NULL } ,
	// indicates search results page
	// this often directly precedes the comment section
	{"[trackback"                   , 0 , 1 , 1 , 0 , NULL , 0 , NULL } ,
	{"class=\"comtext"              , 0 , 8 , 1 , 0 , NULL , 0 , NULL } ,
	{"class=\"comment"              , 0 , 8 , 1 , 0 , NULL , 0 , NULL } ,
	{"class=\"coment"               , 0 , 8 , 1 , 0 , NULL , 0 , NULL } ,
	{"class=\"trackback"            , 0 , 8 , 1 , 0 , NULL , 0 , NULL } ,
	{"class=\"ping"                 , 0 , 8 , 1 , 0 , NULL , 0 , NULL } ,
	{"class=\"followup"             , 0 , 8 , 1 , 0 , NULL , 0 , NULL } ,
	{"class=\"response"             , 0 , 8 , 1 , 0 , NULL , 0 , NULL } ,
	// this can signify a blog entry, not just a comment
	//{"class=\"entry"              , 0 , 8 , 1 , 0 , NULL , 0 , NULL } ,
	// these seem to be more indicative of posted comments
	{"class=\"posted"               , 0 , 8 , 1 , 0 , NULL , 0 },
	{"id=\"posted"                  , 0 , 8 , 1 , 0 , NULL , 0 },
	{"name=\"posted"                , 0 , 8 , 1 , 0 , NULL , 0 },
	// annoying little textbox thingy
	{"class=\"shoutbox"             , 0 , 8 , 1 , 0 , NULL , 0 , NULL } ,
	{"id=\"comment"                 , 0 , 8 , 1 , 0 , NULL , 0 , NULL } ,
	{"id=\"coment"                  , 0 , 8 , 1 , 0 , NULL , 0 , NULL } ,
	{"id=\"trackback"               , 0 , 8 , 1 , 0 , NULL , 0 , NULL } ,
	{"id=\"ping"                    , 0 , 8 , 1 , 0 , NULL , 0 , NULL } ,
	{"id=\"followup"                , 0 , 8 , 1 , 0 , NULL , 0 , NULL } ,
	{"id=\"response"                , 0 , 8 , 1 , 0 , NULL , 0 , NULL } ,
	{"name=\"comment"               , 0 , 8 , 1 , 0 , NULL , 0 , NULL } ,
	{"name=\"coment"                , 0 , 8 , 1 , 0 , NULL , 0 , NULL } ,
	{"name=\"trackback"             , 0 , 8 , 1 , 0 , NULL , 0 , NULL } ,
	{"name=\"ping"                  , 0 , 8 , 1 , 0 , NULL , 0 , NULL } ,
	{"name=\"followup"              , 0 , 8 , 1 , 0 , NULL , 0 , NULL } ,
	{"name=\"response"              , 0 , 8 , 1 , 0 , NULL , 0 , NULL } ,
	// a lot of the comment boards can be identified because
	// they have a bunch of mailto links, one before each comment
	//{"href=\"mailto"                , 0 , 8 , 1 , 0 , NULL , 0 },
	//{"href=mailto"                  , 0 , 8 , 1 , 0 , NULL , 0 },
	// wikipedias
	{"div class=\"editsection"      , 0 , 10, 1 , 0 , NULL , 0 , NULL } ,
	{"action=edit"                  , 0 , 10, 1 , 0 , NULL , 0 , NULL } ,
	// message boards
	{"anonymous user"               , 0 , 10, 1 , 0 , NULL , 0 , NULL } ,
	{"anonymer user"                , 0 , 10, 1 , 0 , NULL , 0 , NULL } ,
	{"date posted"                  , 0 , 10, 1 , 0 , NULL , 0 , NULL } ,
	{"post your notice"             , 0 , 10, 1 , 0 , NULL , 0 , NULL } ,
	{"edit this page"               , 0 , 10, 1 , 0 , NULL , 0 , NULL } ,
	// edit</a><br>
	{"edit<a]br"                    , 0 , 10, 1 , 0 , NULL , 0 , NULL } ,
	// link to edit a comment
	{">edit</a"                     , 0 , 10, 1 , 0 , NULL , 0 , NULL } ,
	// these often indicate blog entries, not just comments
	//{"postedon"                     , 0 , 10, 1 , 0 , NULL , 0 },
	//{"posted by "                   , 0 , 10, 1 , 0 , NULL , 0 },
	//{"posted at "                   , 0 , 10, 1 , 0 , NULL , 0 },
	{"reply with quote"             , 0 , 9 , 0 , 0 , NULL , 0 , NULL } ,
	{">post a reply"                , 0 , 10, 0 , 0 , NULL , 0 , NULL } ,
	{"post reply"                   , 0 , 10, 0 , 0 , NULL , 0 , NULL } ,
	{"submit post"                  , 0 , 10, 0 , 0 , NULL , 0 , NULL } ,
	{">post message"                , 0 , 10, 0 , 0 , NULL , 0 , NULL } ,
	{">post a comment"              , 0 , 10, 0 , 0 , NULL , 0 , NULL } ,
	{">leave a comment"             , 0 , 10, 0 , 0 , NULL , 0 , NULL } ,
	{">post comments"               , 0 , 10, 0 , 0 , NULL , 0 , NULL } ,
	// Comments</font> (0) after each posted entry...
	//{">comments<"                 , 0 , 10, 1 , 0 , NULL , 0 , NULL } ,
	{"comments: <"                  , 0 , 10, 1 , 0 , NULL , 0 , NULL } ,
	{"comments:<"                   , 0 , 10, 1 , 0 , NULL , 0 , NULL } ,
	//{"comment:"                   , 0 , 10, 1 , 0 , NULL , 0 , NULL } ,
	{"reacties:"                    , 0 , 10, 1 , 0 , NULL , 0 , NULL } ,
	{"comentarios:"                 , 0 , 10, 1 , 0 , NULL , 0 , NULL } ,
	{"comentários:"                 , 0 , 10, 1 , 0 , NULL , 0 , NULL } ,
	{">message:"                    , 0 , 10, 0 , 0 , NULL , 0 , NULL } ,
	{">mensagem:"                   , 0 , 10, 0 , 0 , NULL , 0 , NULL } ,
	{">faca seu comentario"         , 0 , 10, 0 , 0 , NULL , 0 , NULL } ,
	{">faça seu comentário"         , 0 , 10, 0 , 0 , NULL , 0 , NULL } ,
	// comment add in german
	{">Kommentar hinzuf"            , 0 , 10, 0 , 0 , NULL , 0 , NULL } ,
	{"rate this link"               , 0 , 10, 0 , 0 , NULL , 0 , NULL } ,
	{"link submit"                  , 0 , 10, 0 , 0 , NULL , 0 , NULL } ,
	{"links directory"              , 0 , 10, 0 , 0 , NULL , 0 , NULL } ,
	{">add my comment"              , 0 , 10, 0 , 0 , NULL , 0 , NULL } ,
	// title of the text area box
	{">your comment"                , 0 , 10, 0 , 0 , NULL , 0 , NULL } ,
	{"your comment<"                , 0 , 10, 0 , 0 , NULL , 0 , NULL } ,
	{">comment by"                  , 0 , 10, 1 , 0 , NULL , 0 , NULL } ,
	{">scrivi un commento"          , 0 , 10, 0 , 0 , NULL , 0 , NULL } ,
	{">scrivi il tuo commento"      , 0 , 10, 0 , 0 , NULL , 0 , NULL } ,
	{"add comment"                  , 0 , 10, 0 , 0 , NULL , 0 , NULL } ,
	{"trackbacks for the art"       , 0 , 12, 1 , 0 , NULL , 0 , NULL } ,
	{"these trackbacks have been re", 0 , 13, 1 , 0 , NULL , 0 , NULL } ,
	{"trackback pings"              , 0 , 13, 1 , 0 , NULL , 0 , NULL } ,
	{"read the rest of this com"    , 0 , 13, 1 , 0 , NULL , 0 , NULL } ,
	// that was the opinion of ...
	{"das war die meinung von"      , 0 , 13, 1 , 0 , NULL , 0 , NULL } ,
	{"resource partner"             , 0 , 49, 0 , 0 , NULL , 0 , NULL } ,
	{"partner link"                 , 0 , 50, 0 , 0 , NULL , 0 , NULL } ,
	{"partner site"                 , 0 , 51, 0 , 0 , NULL , 0 , NULL } ,
	{"sign the guestbook"           , 0 , 43, 0 , 0 , NULL , 0 , NULL } ,
	//{"add new comment"              , 0 , 14, 0 , 0 , NULL , 0 },
	//{"add message"                  , 0 , 14, 0 , 0 , NULL , 0 },
	// tagboard software allows free submits. it has this in 
	// an html comment tag...
	{"2002 natali ardianto"         , 0 , 14, 0 , 0 , NULL , 0 , NULL } ,
	// guestbooks
	{"guestbook</title"             , 0 , 13, 0 , 0 , NULL , 0 , NULL } ,
	{"gastenboek</title"            , 0 , 13, 0 , 0 , NULL , 0 , NULL } ,
	// link management software puts a search box on there
	{"search our links"             , 0 , 14, 0 , 0 , NULL , 0 , NULL } ,
	{"find all words option"        , 0 , 14, 0 , 0 , NULL , 0 , NULL } ,
	// link exchange indicators
	{"link you want to share"       , 0 , 14, 0 , 0 , NULL , 0 , NULL } ,
	{"link trader"                  , 0 , 14, 0 , 0 , NULL , 0 , NULL } ,
	{"link exchange"                , 0 , 15, 0 , 0 , NULL , 0 , NULL } ,
	{"link partner"                 , 0 , 16, 0 , 0 , NULL , 0 , NULL } ,
	{"link xchange"                 , 0 , 17, 0 , 0 , NULL , 0 , NULL } ,
	{"link swap"                    , 0 , 18, 0 , 0 , NULL , 0 , NULL } ,
	{"links trader"                 , 0 , 19, 0 , 0 , NULL , 0 , NULL } ,
	{"links exchange"               , 0 , 20, 0 , 0 , NULL , 0 , NULL } ,
	{"links partner"                , 0 , 21, 0 , 0 , NULL , 0 , NULL } ,
	{"links xchange"                , 0 , 22, 0 , 0 , NULL , 0 , NULL } ,
	{"links swap"                   , 0 , 23, 0 , 0 , NULL , 0 , NULL } ,
	{"list your site"               , 0 , 26, 0 , 0 , NULL , 0 , NULL } ,
	{"add your web site"            , 0 , 24, 0 , 0 , NULL , 0 , NULL } ,
	{"add your website"             , 0 , 25, 0 , 0 , NULL , 0 , NULL } ,
	{"add your site"                , 0 , 26, 0 , 0 , NULL , 0 , NULL } ,
	{"add your link"                , 0 , 27, 0 , 0 , NULL , 0 , NULL } ,
	{"add your url"                 , 0 , 28, 0 , 0 , NULL , 0 , NULL } ,
	{"add site"                     , 0 , 28, 0 , 0 , NULL , 0 , NULL } ,
	// email the webmaster to have your link on this page
	{"have your link"               , 0 , 28, 0 , 0 , NULL , 0 , NULL } ,
	{"add a web site"               , 0 , 29, 0 , 0 , NULL , 0 , NULL } ,
	{"add a website"                , 0 , 30, 0 , 0 , NULL , 0 , NULL } ,
	{"add a site"                   , 0 , 31, 0 , 0 , NULL , 0 , NULL } ,
	{"add a link"                   , 0 , 32, 0 , 0 , NULL , 0 , NULL } ,
	{"add a url"                    , 0 , 33, 0 , 0 , NULL , 0 , NULL } ,
	{"adding your web site"         , 0 , 34, 0 , 0 , NULL , 0 , NULL } ,
	{"adding your website"          , 0 , 35, 0 , 0 , NULL , 0 , NULL } ,
	{"adding your site"             , 0 , 36, 0 , 0 , NULL , 0 , NULL } ,
	{"adding your link"             , 0 , 37, 0 , 0 , NULL , 0 , NULL } ,
	{"adding your url"              , 0 , 38, 0 , 0 , NULL , 0 , NULL } ,
	{"adding a web site"            , 0 , 39, 0 , 0 , NULL , 0 , NULL } ,
	{"adding a website"             , 0 , 40, 0 , 0 , NULL , 0 , NULL } ,
	{"adding a site"                , 0 , 41, 0 , 0 , NULL , 0 , NULL } ,
	{"adding a link"                , 0 , 42, 0 , 0 , NULL , 0 , NULL } ,
	{"adding a url"                 , 0 , 43, 0 , 0 , NULL , 0 , NULL } ,
	{"add url"                      , 0 , 43, 0 , 0 , NULL , 0 , NULL } ,
	{"add resource"                 , 0 , 43, 0 , 0 , NULL , 0 , NULL } ,
	{"add link"                     , 0 , 43, 0 , 0 , NULL , 0 , NULL } ,
	{"add free link"                , 0 , 43, 0 , 0 , NULL , 0 , NULL } ,
	{"addlink"                      , 0 , 43, 0 , 0 , NULL , 0 , NULL } ,
	{"suggest a site"               , 0 , 43, 0 , 0 , NULL , 0 , NULL } ,
	{"swap links"                   , 0 , 43, 0 , 0 , NULL , 0 , NULL } ,
	{"considered for addition"      , 0 , 43, 0 , 0 , NULL , 0 , NULL } ,
	{"we are not affiliated"        , 0 , 43, 0 , 0 , NULL , 0 , NULL } ,
	{"have a site to add"           , 0 , 43, 0 , 0 , NULL , 0 , NULL } ,
	{"submit your web site"         , 0 , 34, 0 , 0 , NULL , 0 , NULL } ,
	{"submit your website"          , 0 , 35, 0 , 0 , NULL , 0 , NULL } ,
	{"submit your site"             , 0 , 36, 0 , 0 , NULL , 0 , NULL } ,
	{"submit your link"             , 0 , 37, 0 , 0 , NULL , 0 , NULL } ,
	{"submit your url"              , 0 , 38, 0 , 0 , NULL , 0 , NULL } ,
	{"submit a web site"            , 0 , 39, 0 , 0 , NULL , 0 , NULL } ,
	{"submit a website"             , 0 , 40, 0 , 0 , NULL , 0 , NULL } ,
	{"submit a site"                , 0 , 41, 0 , 0 , NULL , 0 , NULL } ,
	{"submit a link"                , 0 , 42, 0 , 0 , NULL , 0 , NULL } ,
	{"submit link"                  , 0 , 42, 0 , 0 , NULL , 0 , NULL } ,
	{"submit a url"                 , 0 , 43, 0 , 0 , NULL , 0 , NULL } ,
	// . article spammers using article-emporium.com, etc.
	// . these articles get circulated into regular websites
	{"submit your article"          , 0 , 43, 0 , 0 , NULL , 0 , NULL } ,
	{"submit articles"              , 0 , 43, 0 , 0 , NULL , 0 , NULL } ,
	{"submit an article"            , 0 , 43, 0 , 0 , NULL , 0 , NULL } ,
	{"for any feedback contact"     , 0 , 43, 0 , 0 , NULL , 0 , NULL } ,
	{"for any feedback mail"        , 0 , 43, 0 , 0 , NULL , 0 , NULL } ,
	{"for any feedback email"       , 0 , 43, 0 , 0 , NULL , 0 , NULL } ,
	{"other articles that might"    , 0 , 43, 0 , 0 , NULL , 0 , NULL } ,
	{"is a freelance"               , 0 , 43, 0 , 0 , NULL , 0 , NULL } ,
	{"author is an amateur"         , 0 , 43, 0 , 0 , NULL , 0 , NULL } ,
	{"article source"               , 0 , 43, 0 , 0 , NULL , 0 , NULL } ,
	{"word count:"                  , 0 , 43, 0 , 0 , NULL , 0 , NULL } ,
	{"for additional information on", 0 , 43, 1 , 0 , NULL , 0 , NULL } ,
	{"for more information on"      , 0 , 43, 1 , 0 , NULL , 0 , NULL } ,
	{"for further assistance visit" , 0 , 43, 1 , 0 , NULL , 0 , NULL } ,
	{"article submitted on"         , 0 , 43, 0 , 0 , NULL , 0 , NULL } ,
	{"please rate this"             , 0 , 43, 0 , 0 , NULL , 0 , NULL } ,
	{"rate the article"             , 0 , 43, 0 , 0 , NULL , 0 , NULL } ,
	//{"how would you rate"         , 0 , 43, 0 , 0 , NULL , 0 , NULL } ,
	{"add rating"                   , 0 , 43, 0 , 0 , NULL , 0 , NULL } ,
	{"trade text link"              , 0 , 44, 0 , 0 , NULL , 0 , NULL } ,
	{"trade link"                   , 0 , 45, 0 , 0 , NULL , 0 , NULL } ,
	{"exchange link"                , 0 , 46, 0 , 0 , NULL , 0 , NULL } ,
	{"exchanging link"              , 0 , 47, 0 , 0 , NULL , 0 , NULL } ,
	{"reciprocal link"              , 0 , 48, 0 , 0 , NULL , 0 , NULL } ,

	// new stuff
	{">sponsors<"                   , 0 , 48, 0 , 0 , NULL , 0 , NULL } ,
	{">sponsor<"                    , 0 , 48, 0 , 0 , NULL , 0 , NULL } ,
	{">sponsored<"                  , 0 , 48, 0 , 0 , NULL , 0 , NULL } ,
	{">submit site<"                , 0 , 48, 0 , 0 , NULL , 0 , NULL } ,
	{": sponsor"                    , 0 , 48, 0 , 0 , NULL , 0 , NULL } ,
	{"/sponsor/"                    , 0 , 48, 0 , 0 , NULL , 0 , NULL } ,
	{"*sponsors*"                   , 0 , 48, 0 , 0 , NULL , 0 , NULL } ,
	{">payperpost"                  , 0 , 48, 0 , 0 , NULL , 0 , NULL } ,
	{"sponsored post"               , 0 , 48, 0 , 0 , NULL , 0 , NULL } ,
	{"sponsored flag"               , 0 , 48, 0 , 0 , NULL , 0 , NULL } ,
	{"sponsoredflag"                , 0 , 48, 0 , 0 , NULL , 0 , NULL } ,
	{"sponsored listing"            , 0 , 48, 1 , 0 , NULL , 0 , NULL } ,
	{"sponsored link"               , 0 , 48, 1 , 0 , NULL , 0 , NULL } ,
	{"post is sponsor"              , 0 , 48, 0 , 0 , NULL , 0 , NULL } ,
	{"paid post"                    , 0 , 48, 0 , 0 , NULL , 0 , NULL } ,
	{"powered by"                   , 0 , 48, 0 , 0 , NULL , 0 , NULL } , // wordpress
	{"suggest your website"         , 0 , 48, 0 , 0 , NULL , 0 , NULL } ,
	{"advertisement:"               , 0 , 48, 1 , 0 , NULL , 0 , NULL } 
};

// now check outlinks on the page for these substrings
static Needle s_needles2[] = {
	{"cyber-robotics.com" , 0 , 0 , 0 , 0 , NULL , 0 , NULL } ,
	{"cyberspacehq.com"   , 0 , 0 , 0 , 0 , NULL , 0 , NULL } ,
	{"links4trade.com"    , 0 , 0 , 0 , 0 , NULL , 0 , NULL } ,
	{"searchfeed.com"     , 0 , 0 , 0 , 0 , NULL , 0 , NULL } ,
	{"marketnex.com"      , 0 , 0 , 0 , 0 , NULL , 0 , NULL } ,
	{"partnersignup"      , 0 , 0 , 0 , 0 , NULL , 0 , NULL } ,
	{"publisher-network"  , 0 , 0 , 0 , 0 , NULL , 0 , NULL } ,
	//{"amazon.com"       , 0 , 0 , 0 , 0 , NULL , 0 , NULL } ,
	//{"dmoz.org"         , 0 , 0 , 0 , 0 , NULL , 0 , NULL } ,
	//{"dmoz.com"         , 0 , 0 , 0 , 0 , NULL , 0 , NULL } ,
	{"linksmanager"       , 0 , 0 , 0 , 0 , NULL , 0 , NULL } ,
	{"changinglinks"      , 0 , 0 , 0 , 0 , NULL , 0 , NULL } 
};


// . we set the bit in linkdb for a doc if this returns true
// . it precludes a doc from voting if its bits is set in linkdb
// . this saves resources
// . the isLinkSpam() function is used when we have the linkee url
// . note is only set if the whole doc can not vote for some reason
// . otherwise, each outlink in "links" is assigned a "note" to indicate if 
//   the outlink is a spam link or not
// . returns true on success, false on error
bool setLinkSpam ( int32_t       ip                 ,
		   int32_t      *indCatIds          ,
		   int32_t       numIndCatIds       ,
		   Url       *linker             ,
		   int32_t       siteNumInlinks     ,
		   Xml       *xml                ,
		   Links     *links              ,
		   bool       isContentTruncated ,
		   int32_t       niceness           ) {
	// get our url
	//Url *linker = tr->getUrl();
	// it is critical to get inlinks from all pingserver xml
	// pages regardless if they are often large pages. we
	// have to manually hard-code the ping servers in for now.
	if ( linker->isPingServer() ) return false;
	// if the doc got truncated we may be missing valuable identifiers
	// that identify the doc as a guestbook or something
	if ( isContentTruncated )
		return links->setAllSpamBits("doc too big");
	// get linker quality
	//int32_t q = tr->getDocQuality();
	// do not allow .info or .biz to vote ever for now
	char *tld    = linker->getTLD();
	int32_t  tldLen = linker->getTLDLen();
	if ( tldLen == 4 && strncmp ( tld, "info" , tldLen) == 0 && //q < 55 )
	     siteNumInlinks < 20 )
		return links->setAllSpamBits("low quality .info linker");
	if ( tldLen == 3 && strncmp ( tld, "biz" , tldLen) == 0 && //q < 55 )
	     siteNumInlinks < 20 )
		return links->setAllSpamBits("low quality .biz linker");

	// if has an outlink to dmoz-identified porn, all outlinks are spam
	int32_t *ids  = indCatIds;//NULL
	int32_t  nids = numIndCatIds;//0;
	//if ( tr ) ids  = tr->getIndCatids();
	//if ( tr ) nids = tr->getNumIndCatids();
	for ( int32_t j = 0 ; j < nids ; j++ ) 
		if ( g_categories && g_categories->isIdAdult ( ids[j] ) )
			return links->setAllSpamBits("dmoz porn");

	QUICKPOLL( niceness );
	// do we contain a dmoz subpath in our url? that would indicate that
	// we are probably a dmoz mirror!
	char *zstart = linker->getPath();
	int32_t  zlen   = linker->getPathLen();
	char *zend   = zstart + zlen;
	// start at the end of the path
	char *z      = zend-1;
	// back up to previous /
	for ( ; z > zstart && *z != '/' ; z-- );
	// make that the new end
	zend = z + 1;
	// need at least 2 path components before checking... keep count
	int32_t  zcount = 2;
	// begin the loop
	while ( z > zstart ) {
		// . backup until we hit the previous /
		for ( z-- ; z > zstart && *z != '/' ; z-- );
		// debug
		//char tmp[2000];
		//gbmemcpy(tmp,z,zend-z);
		//tmp[zend-z]=0;
		//log("build: path=%s",tmp);
		// look it up
		// "/Arts/" --> 1
		if ( --zcount > 0 ) continue;
		if ( g_categories->getIndexFromPath (z, zend-z) < 0 ) continue;
		// consider ourselves a dmoz mirror and discount all outlinks
		return links->setAllSpamBits("dmoz subpath in url");
	}

	QUICKPOLL( niceness );
	// guestbook in hostname - domain?
	char *hd  = linker->getHost();
	char *hd2 = linker->getDomain();
	int32_t  hdlen = hd2 - hd;
	if ( hd && hd2 && hdlen < 30 ) {
		char c = hd[hdlen];
		hd[hdlen] = '\0';
		bool hasIt = false;
		if ( strstr ( hd , "guestbook" ) ) hasIt = true;
		hd[hdlen] = c;
		if ( hasIt ) 
			return links->setAllSpamBits("guestbook in hostname");
	}

	// do not allow any cgi url to vote
	if ( linker->isCgi() )
		return links->setAllSpamBits("path is cgi");

	int32_t plen = linker->getPathLen();
	// if the page has just one rel=nofollow tag then we know they
	// are not a guestbook
	//if ( links->hasRelNoFollow() ) plen = 0;
	if ( plen > 1 ) {
		char *p    = linker->getPath();
		//char  c    = p[plen-1];
		//p[plen-1] = '\0';
		//bool val = false;
		char *note = NULL;
		if ( strncasestr ( p , "guest",plen,5) ) 
			note = "path has guest"          ;
		else if ( strncasestr ( p , "cgi",plen,3) ) 
			note = "path has cgi"            ;
		else if ( strncasestr ( p , "gast",plen,4) ) 
			note = "path has gast"           ;
		// german
		else if ( strncasestr ( p , "gaest",plen,5) )
			note = "path has gaest"          ;
		else if ( strncasestr ( p , "gbook",plen,5) ) 
			note = "path has gbook"          ;
		// vietnamese?
		else if ( strncasestr ( p , "akobook",plen,7) ) 
			note = "path has akobook"        ;
		else if ( strncasestr ( p , "/gb",plen,3) ) 
			note = "path has /gb"            ;
		else if ( strncasestr ( p , "msg",plen,3 ) ) 
			note = "path has msg"            ;
		else if ( strncasestr ( p , "messag",plen,6) ) 
			note = "path has messag"         ;
		else if ( strncasestr ( p , "board",plen,5) ) 
			note = "path has board"          ;
		else if ( strncasestr ( p , "coment",plen,6) ) 
			note = "path has coment"         ;
		else if ( strncasestr ( p , "comment",plen,7) ) 
			note = "path has comment"        ;
		else if ( strncasestr ( p , "linktrader",plen,10) ) 
			note = "path has linktrader"     ;
		else if ( strncasestr ( p , "tradelinks",plen,10) ) 
			note = "path has tradelinks"     ;
		else if ( strncasestr ( p , "trade-links",plen,11) ) 
			note = "path has trade-links"    ;
		else if ( strncasestr ( p , "linkexchange",plen,12) ) 
			note = "path has linkexchange"   ;
		else if ( strncasestr ( p , "link-exchange",plen,13  ) )
			note = "path has link-exchange"  ;
		else if ( strncasestr ( p , "reciprocal-link",plen,15) )
			note = "path has reciprocal-link";
		else if ( strncasestr ( p , "reciprocallink",plen, 14) )
			note = "path has reciprocallink" ;
		else if ( strncasestr ( p , "/trackbacks/",plen,12 ) ) 
			note = "path has /trackbacks/"   ;
		//if ( gb_strcasestr ( p , "archive"   ) ) val = true;
		//if ( gb_strcasestr ( p , ".asp"      ) ) val = true;
		//if ( gb_strcasestr ( p , ".aspx"     ) ) val = true;
		// these are mostly link exchange pages. no they are not!
		//if ( gb_strcasestr ( p , "link"   ) ) val = true;
		//p[plen-1] = c;
		//if ( val ) { note = "cgi or guestbook url"; return true; }
		if ( note ) return links->setAllSpamBits(note);
	}

	QUICKPOLL( niceness );

	// does title contain "web statistics for"?
	int32_t  tlen ;
	char *title = xml->getString ( "title" , &tlen );
	if ( title && tlen > 0 ) {
		// normalize title into buffer, remove non alnum chars
		char buf[256];
		char *d    = buf;
		char *dend = buf + 250;
		char *s    = title;
		char *send = title + tlen;
		while ( d < dend && s < send ) {
			// remove punct
			if ( ! is_alnum_a(*s) ) { s++; continue; }
			*d = to_lower_a ( *s );
			d++;
			s++;
		}
		*d = '\0';
		// see if it matches some catch phrases
		bool val = false;
		if      ( strstr (buf,"webstatisticsfor"      )) val = true;
		if      ( strstr (buf,"webserverstatisticsfor")) val = true;
		else if ( strstr (buf,"usagestatisticsfor"    )) val = true;
		else if ( strstr (buf,"siteusageby"           )) val = true;
		else if ( strstr (buf,"surfstatsloganal"      )) val = true;
		else if ( strstr (buf,"webstarterhelpstats"   )) val = true;
		else if ( strstr (buf,"sitestatistics"        )) val = true;
		if ( val ) return links->setAllSpamBits("stats page");
	}

	QUICKPOLL( niceness );

	/////////////////////////////////////////////////////
	//
	// check content for certain keywords and phrases
	//
	/////////////////////////////////////////////////////

	//char *haystack     = tr->getContent();
	//int32_t  haystackSize = tr->getContentLen();
	char *haystack     = xml->getContent();
	int32_t  haystackSize = xml->getContentLen();

	// get our page quality, it serves as a threshold for some algos
	//char quality = tr->getNewQuality();

	//char *linkPos = NULL;
	//if ( linkNode >= 0 ) linkPos = xml->getNode ( linkNode );

	//if ( strstr ( linker->getUrl() , "usa_apartments1.htm") ) {
	//	log("hey");
	//	sleep(7);
	//}

	// loop:
	// do not call them "bad links" if our link occurs before any
	// comment section. our link's position therefore needs to be known,
	// that is why we pass in linkPos. 
	// "n" is the number it matches.
	int32_t numNeedles1 = sizeof(s_needles1)/sizeof(Needle);
	bool hadPreMatch;
	getMatches2 ( s_needles1 ,
		      numNeedles1      ,
		      haystack         ,
		      haystackSize     ,
		      NULL             , // linkPos          ,
		      NULL             , // &n               ,
		      false            , // stopAtFirstMatch
		      &hadPreMatch     ,
		      true             , // save quicktables
		      niceness         );

	QUICKPOLL( niceness );

	// see if we got a hit
	char *minPtr = NULL;
	char *note   = NULL;
	for ( int32_t i = 0 ; i < numNeedles1 ; i++ ) {
		// open.thumbshots.org needs multiple counts
		if ( i == 0 && s_needles1[i].m_count < 5 ) continue;
		// skip if no matches on this string
		if ( s_needles1[i].m_count <= 0  ) continue;
		// ok, if it had its section bit set to 0 that means the 
		// whole page is link spam!
		if ( s_needles1[i].m_isSection == 0 )
			return links->setAllSpamBits(s_needles1[i].m_string );
		// get the char ptr
		char *ptr = s_needles1[i].m_firstMatch;
		// set to the min
		if ( ! minPtr || ptr < minPtr ) { 
			note   = s_needles1[i].m_string; 
			minPtr = ptr;
		}
	}
	QUICKPOLL( niceness );

	// convert the char ptr into a link node following it
	int32_t aa = 0;
	if ( minPtr ) aa = links->getNumLinks();
	int32_t mini = -1;
	for ( int32_t i = 0 ; i < aa ; i++ ) {
		// get the link's char ptr into the content
		int32_t  linkNode = links->getNodeNum(i);
		char *linkPos  = NULL;
		if ( linkNode >= 0 ) linkPos = xml->getNode ( linkNode );
		// now we can compare, if BEFORE this comment section
		// indicating tag, we are NOT link spam, so continue
		if ( linkPos < minPtr ) continue;
		// otherwise, we are the first, stop.
		mini = i;
		break;
	}
	QUICKPOLL( niceness );

	// now count all the links BELOW this match as link spam
	// but everyone else is ok!
	if ( minPtr && mini >= 0 ) 
		links->setSpamBits ( note , mini );

	// now check outlinks on the page for these substrings
	haystack     = links->getLinkBuf();
	haystackSize = links->getLinkBufLen();
	int32_t numNeedles2 = sizeof(s_needles2)/sizeof(Needle);
	getMatches2 ( s_needles2   , 
		      numNeedles2  , 
		      haystack     , 
		      haystackSize , 
		      NULL         ,  // linkPos, 
		      NULL         ,  // &n ,
		      false        ,  // stopAtFirstMatch?
		      NULL         ,
		      true         ,  // save quicktables
		      niceness     );

	QUICKPOLL( niceness );

	// see if we got a hit
	for ( int32_t i = 0 ; i < numNeedles2 ; i++ ) {
		// skip if did not match
		if ( s_needles2[i].m_count <= 0 ) continue;
		// the whole doc is considered link spam
		return links->setAllSpamBits(s_needles2[i].m_string); 
	}

	QUICKPOLL( niceness );

	//skiplinks:
	// check for certain post tag, indicative of a comment-friendly blog
	// <form method=post ... action=*comments*cgi-bin>
	// <form method="post" 
	//       action="http://www.mydomain.com/cgi-bin/mt-comments.cgi" 
	//       name="comments_form" ...>
	// <form method=POST 
	//  action="http://peaceaction.org/wboard/wwwboard.cgi">
	int32_t nn = xml->getNumNodes();
	bool gotTextArea = false;
	bool gotSubmit   = false;
	for ( int32_t i=0; i < nn ; i++ ) {
		// <textarea> tags are bad... but only if we have not
		// matched "track" or whatever from above... check for that
		// if you uncommment this... otherwise you disable all blogs!
		// Only do this check if we did match a comment related phrase
		// in s_needles1[] BUT it was BEFORE our outlink. That 
		// basically means that we do *not* recognize the format of 
		// the comment page and so therefore need to be more 
		// restrictive about allowing this page to vote.
		if ( ! hadPreMatch ) {
			// is it a <textarea> tag?
			if ( xml->getNodeId ( i ) == TAG_TEXTAREA ) 
				gotTextArea = true;
			// is it an <input> tag?
			int32_t len = 0;
			if ( xml->getNodeId ( i ) == TAG_INPUT &&
			     xml->getString(i,"submit",&len)) gotSubmit = true;
		}
		// check for script tag
		/*
		if ( xml->getNodeId(i) == TAG_SCRIPT && quality < 80 ) {
			// <script src=blah.com/fileparse.js" 
			// type="text/javascript"> is used to hide google
			// ads, so don't allow those pages to vote either
			int32_t  slen; xml->getString(i,"src",&slen);
			if ( slen > 0 ) { *note = "script src"; return true; }
		}
		*/
		if ( xml->getNodeId ( i ) != TAG_FORM ) continue;
			
		// get the method field of this base tag
		int32_t  slen;
		char *s = (char *) xml->getString(i,"method",&slen);
		// if not thee, skip it
		if ( ! s || slen <= 0 ) continue;
		//if ( slen != 4 ) continue;
		// if not a post, skip it
		//if ( strncasecmp ( s , "post" , 4 ) ) continue;
		// get the action url
		s = (char *) xml->getString(i,"action",&slen);
		if ( ! s || slen <= 0 ) continue;
		char c = s[slen];
		s[slen]='\0';
		bool val = false;
		// this is a bit too strong, but i'ev seen an action of
		// "cgi-bin/mt-leaveone.cgi" so we can't rely on "mt-comment"
		if      ( strstr ( s , "comment" ) ) val = true;
		else if ( strstr ( s , "/MT/" ) ) val = true;
		else if ( strstr ( s , "/mt/" ) ) val = true;
		// they can have these search boxes though
		if ( val && strstr ( s , "/mt/mt-search" ) ) val = false;
		//else if ( strstr ( s , "cgi"     ) ) val = true;
		// eliminate some false positives
		//if ( val && strstr ( s , "search" ) ) val = false;
		s[slen] = c;
		if ( val ) return links->setAllSpamBits("post page");
		QUICKPOLL( niceness );
	}

	if ( gotTextArea && gotSubmit )
		return links->setAllSpamBits("textarea tag");

	QUICKPOLL( niceness );

	// edu, gov, etc. can have link chains
	if ( tldLen >= 3 && strncmp ( tld, "edu" , 3) == 0 ) return true;
	if ( tldLen >= 3 && strncmp ( tld, "gov" , 3) == 0 ) return true;
	// . allow sites in dmoz to have link chains, too
	// . no, there are too many spam porn sites in dmoz
	//if ( tr->getNumIndCatids() || tr->getNumCatids()   ) return false;

	// if linker is naughty, he cannot vote... how did he make it in?
	if ( linker->isSpam() )
		return links->setAllSpamBits("linker is sporny"); 

	// . if they link to any adult site, consider them link spam
	// . just consider a 100 link radius around linkNode
	int32_t nl = links->getNumLinks();
	for ( int32_t i = 0 ; i < nl ; i++ ) {
		// skip if this link is internal, we will add it to linkdb
		// anyway... this will save us some processing time
		if ( links->isInternalDom(i) ) continue;
		// otherwise, normalize it...
		Url uu; uu.set ( links->getLink(i), links->getLinkLen(i) );
		char          *h     = uu.getUrl();
		// include the trailing /
		char          *hend  = uu.getHost() + uu.getHostLen() + 1;
		uint32_t  hhash = hash32 ( h , hend - h );
		if ( g_categories && g_categories->isInBadCat ( hhash ) ) {
			links->setAllSpamBits("links to dmoz filth");
			log(LOG_DEBUG,"build: %s is filthy.",uu.getUrl());
			return true;
		}
		// take a break
		QUICKPOLL ( niceness );
		// . is it near sporny links? (naughty domains or lotsa -'s)
		// . if we are in a list of ads, chances are good the true
		//   nature of the ads will emerge...
		if ( uu.isSpam() ) {
			links->setAllSpamBits("has sporny outlinks");
			log(LOG_DEBUG,"build: %s has sporny outlinks.",
			    uu.getUrl());
			return true;
		}

		// check if this url is a link chain
		//if ( q >= 60 ) continue;
		if ( siteNumInlinks >= 50 ) continue;
		char *np = NULL;
		//if ( strncmp("http://www.reliant.com",uu.getUrl(),18)==0 )
		//     log("hey");
		// get the xml node of link #i
		int32_t xmlNode = links->getNodeNum ( i );
		if ( isLinkChain ( xml , linker, &uu, xmlNode, &np ))
			links->setSpamBit ( np , i );
		// take a break
		QUICKPOLL ( niceness );
	}
	return true;
}



bool isLinkSpam ( Url *linker, 
		  int32_t ip ,
		  int32_t *indCatIds ,
		  int32_t  numIndCatIds ,
		  int32_t siteNumInlinks ,
		  //TitleRec *tr, 
		  Xml *xml, 
		  Links *links ,
		  int32_t maxDocLen , 
		  char **note , 
		  Url *linkee , 
		  // node position of the linkee in the linker's content
		  int32_t  linkNode ,
		  char *coll     ,
		  int32_t  niceness ) {
	// it is critical to get inlinks from all pingserver xml
	// pages regardless if they are often large pages. we
	// have to manually hard-code the ping servers in for now.
	if ( linker->isPingServer() ) return false;
	// same host linkers can be link spam (TODO: make same ip block)
	// because we only allow up to 10 to vote as a single voter
	if ( linkee ) {
		char *h1    = linkee->getHost();
		int32_t  h1len = linkee->getHostLen();
		char *h2    = NULL;
		int32_t  h2len = 0;
		//if ( tr ) h2    = tr->getUrl()->getHost();
		//if ( tr ) h2len = tr->getUrl()->getHostLen();
		h2    = linker->getHost();
		h2len = linker->getHostLen();
		if ( h1len == h2len && strncmp ( h1 , h2 , h1len ) == 0 ) 
			return false;
	}
	// do not allow .info or .biz to vote ever for now
	char *tld    = linker->getTLD();
	int32_t  tldLen = linker->getTLDLen();
	if ( tldLen == 4 && strncmp ( tld, "info" , tldLen) == 0 ) {
		*note = ".info tld";
		return true;
	}
	if ( tldLen == 3 && strncmp ( tld, "biz" , tldLen) == 0 ) {
		*note = ".biz tld";
		return true;
	}
	//int32_t ip = tr->getIp();
	char *ips = (char *)&ip;
	if ( ips[0]==70 && ips[1]>=80 && ips[1]<=89 ) {
		*note = "70.8*. ip";
		return true;
	}

	// if this page fails zak's page quality algo, do not let it vote
	// like if from .biz or lots of hyphens in the url or an ip-based 
	// url.
	// now we do this on a site by site basis, more accurate that way.
	//  --z
	//unsigned char spamScore = tr->getSpamScore();
	// 	unsigned char spamScore ;
	// 	spamScore = getNegativeQualityWeight ( linker ,
	// 					       xml   ,
	// 					       links ,
	// 					       NULL  , // words
	// 					       coll  ,
	// 					       NULL  , // sr    ,
	// 					       NULL  ,// safebuf
	// 					       0  /*niceness*/); 
	// 	if ( spamScore >= 30 ) {
	// 		*note = "had big spam score";
	// 		return true;
	// 	}

	// big pages may have keywords identifying them as log pages cutoff
	// so assume the worst
	//if ( tr->getContentLen() > 100*1024 ) return true;
	// i saw a german doc get its textarea cut out because of this, so
	// we need this here
	//if ( tr && tr->getContentLen() > maxDocLen ) {
	if ( xml && xml->getContentLen() > maxDocLen ) {
		*note ="doc too big";
		return true; 
	}
	// if it has a link to dmoz.org then we are probably a dmoz mirror 
	// because we are required to have a link to dmoz if a mirror
	bool checkForDmoz = true;
	bool checkForAmazon = true;
	if ( linkee && 
	     linkee->getDomainLen() == 8 &&
	     linkee->getDomain() )
		if ( strncmp ( linkee->getDomain() , "dmoz.org" , 8 ) == 0 ||
		     strncmp ( linkee->getDomain() , "dmoz.com" , 8 ) == 0 )
			checkForDmoz = false;

	if ( linker && 
	     linker->getDomainLen() == 8 &&
	     linker->getDomain() )
		if ( strncmp ( linker->getDomain() , "dmoz.org" , 8 ) == 0 ||
		     strncmp ( linker->getDomain() , "dmoz.com" , 8 ) == 0 )
			checkForDmoz = false;

	// does the url of the linker have a dmoz path in it?
	char *zstart = linker->getPath();
	int32_t  zlen   = linker->getPathLen();
	char *zend   = zstart + zlen;
	// start at the end of the path
	char *z      = zend-1;
	// back up to previous /
	for ( ; z > zstart && *z != '/' ; z-- );
	// make that the new end
	zend = z + 1;
	// need at least 2 path components before checking... keep count
	int32_t  zcount = 2;
	// begin the loop
	while ( checkForDmoz && z > zstart ) {
		// . backup until we hit the previous /
		for ( z-- ; z > zstart && *z != '/' ; z-- );
		// debug
		//char tmp[2000];
		//gbmemcpy(tmp,z,zend-z);
		//tmp[zend-z]=0;
		//log("build: path=%s",tmp);
		// look it up
		// "/Arts/" --> 1
		if ( --zcount > 0 ) continue;
		if ( g_categories->getIndexFromPath (z, zend-z) < 0 ) continue;
		*note = "has dmoz path";
		return true;
	}

	QUICKPOLL( niceness );

	// if it has a link to amazon.com
	// because we are required to have a link to dmoz if a mirror
	if ( linkee && linkee->getDomain() &&
	     strncmp ( linkee->getDomain(), "amazon.com" , 10 ) == 0 )
		checkForAmazon = false;

	if ( linker && linker->getDomain() &&
	     strncmp ( linker->getDomain(), "amazon.com" , 10 ) == 0 )
		checkForAmazon = false;

	// guestbook in hostname - domain?
	char *hd  = linker->getHost();
	char *hd2 = linker->getDomain();
	int32_t  hdlen = hd2 - hd;
	if ( hd && hd2 && hdlen < 30 ) {
		char c = hd[hdlen];
		hd[hdlen] = '\0';
		bool hasIt = false;
		if ( strstr ( hd , "guestbook" ) ) hasIt = true;
		hd[hdlen] = c;
		if ( hasIt ) { 
			*note = "guestbook in hostname"; 
			return true; 
		}
	}

	// do not allow any cgi url to vote
	if ( linker->isCgi() ) { *note = "path is cgi"; return true; }

	// breathe
	QUICKPOLL(niceness);

	int32_t plen = linker->getPathLen();

	// if very spammy!!
	// if ( spam

	// if the page has just one rel=nofollow tag then we know they
	// are not a guestbook
	//if ( links->hasRelNoFollow() ) plen = 0;
	if ( plen > 1 ) {
		char *p    = linker->getPath();
		//char  c    = p[plen-1];
		//p[plen-1] = '\0';
		//bool val = false;
		if ( strncasestr ( p , "guest",plen,5) ) {
			*note = "path has guest"          ; return true; }
		else if ( strncasestr ( p , "cgi",plen,3) ) { 
			*note = "path has cgi"            ; return true; }
		else if ( strncasestr ( p , "gast",plen,4) ) { 
			*note = "path has gast"           ; return true; }
		// german
		else if ( strncasestr ( p , "gaest",plen,5) ) {
			*note = "path has gaest"          ; return true; }
		else if ( strncasestr ( p , "gbook",plen,5) ) { 
			*note = "path has gbook"          ; return true; }
		// vietnamese?
		else if ( strncasestr ( p , "akobook",plen,7) ) { 
			*note = "path has akobook"        ; return true; }
		else if ( strncasestr ( p , "/gb",plen,3) ) { 
			*note = "path has /gb"            ; return true; }
		else if ( strncasestr ( p , "msg",plen,3 ) ) { 
			*note = "path has msg"            ; return true; }
		else if ( strncasestr ( p , "messag",plen,6) ) { 
			*note = "path has messag"         ; return true; }
		else if ( strncasestr ( p , "board",plen,5) ) { 
			*note = "path has board"          ; return true; }
		else if ( strncasestr ( p , "coment",plen,6) ) { 
			*note = "path has coment"         ; return true; }
		else if ( strncasestr ( p , "comment",plen,7) ) { 
			*note = "path has comment"        ; return true; }
		else if ( strncasestr ( p , "linktrader",plen,10) ) { 
			*note = "path has linktrader"     ; return true; }
		else if ( strncasestr ( p , "tradelinks",plen,10) ) { 
			*note = "path has tradelinks"     ; return true; }
		else if ( strncasestr ( p , "trade-links",plen,11) ) { 
			*note = "path has trade-links"    ; return true; }
		else if ( strncasestr ( p , "linkexchange",plen,12) ) { 
			*note = "path has linkexchange"   ; return true; }
		else if ( strncasestr ( p , "link-exchange",plen,13  ) ) {
			*note = "path has link-exchange"  ; return true; }
		else if ( strncasestr ( p , "reciprocal-link",plen,15) ) {
			*note = "path has reciprocal-link"; return true; }
		else if ( strncasestr ( p , "reciprocallink",plen, 14) ) {
			*note = "path has reciprocallink" ; return true; }
		else if ( strncasestr ( p , "/trackbacks/",plen,12 ) ) { 
			*note = "path has /trackbacks/"   ; return true; }

		//if ( gb_strcasestr ( p , "archive"   ) ) val = true;
		//if ( gb_strcasestr ( p , ".asp"      ) ) val = true;
		//if ( gb_strcasestr ( p , ".aspx"     ) ) val = true;
		// these are mostly link exchange pages. no they are not!
		//if ( gb_strcasestr ( p , "link"   ) ) val = true;
		//p[plen-1] = c;
		//if ( val ) { *note = "cgi or guestbook url"; return true; }
	}

	QUICKPOLL( niceness );

	// scan through the content as fast as possible
	char  *content    = xml->getContent(); 
	int32_t   contentLen = xml->getContentLen();


	// does title contain "web statistics for"?
	int32_t  tlen ;
	char *title = xml->getString ( "title" , &tlen );
	if ( title && tlen > 0 ) {
		// normalize title into buffer, remove non alnum chars
		char buf[256];
		char *d    = buf;
		char *dend = buf + 250;
		char *s    = title;
		char *send = title + tlen;
		while ( d < dend && s < send ) {
			// remove punct
			if ( ! is_alnum_a(*s) ) { s++; continue; }
			*d = to_lower_a ( *s );
			d++;
			s++;
		}
		*d = '\0';
		// see if it matches some catch phrases
		bool val = false;
		if      ( strstr (buf,"webstatisticsfor"      )) val = true;
		if      ( strstr (buf,"webserverstatisticsfor")) val = true;
		else if ( strstr (buf,"usagestatisticsfor"    )) val = true;
		else if ( strstr (buf,"siteusageby"           )) val = true;
		else if ( strstr (buf,"surfstatsloganal"      )) val = true;
		else if ( strstr (buf,"webstarterhelpstats"   )) val = true;
		else if ( strstr (buf,"sitestatistics"        )) val = true;
		if ( val ) { *note = "stats page"; return true; }
	}

	QUICKPOLL( niceness );

	/////////////////////////////////////////////////////
	//
	// check content for certain keywords and phrases
	//
	/////////////////////////////////////////////////////

	char *haystack     = content;
	int32_t  haystackSize = contentLen;

	// get our page quality, it serves as a threshold for some algos
	//char quality = tr->getNewQuality();

	char *linkPos = NULL;
	if ( linkNode >= 0 ) linkPos = xml->getNode ( linkNode );

	//if ( strstr ( linker->getUrl() , "usa_apartments1.htm") ) {
	//	log("hey");
	//	sleep(7);
	//}

	// loop:
	// do not call them "bad links" if our link occurs before any
	// comment section. our link's position therefore needs to be known,
	// that is why we pass in linkPos. 
	// "n" is the number it matches.
	int32_t  n;
	int32_t numNeedles1 = sizeof(s_needles1)/sizeof(Needle);
	bool hadPreMatch;
	getMatches2 ( s_needles1       ,
		      numNeedles1      ,
		      haystack         ,
		      haystackSize     ,
		      linkPos          ,
		      &n               ,
		      false            , // stopAtFirstMatch
		      &hadPreMatch     ,
		      true             , // save quicktables
		      niceness         );

	QUICKPOLL( niceness );

	// see if we got a hit
	for ( int32_t i = 0 ; i < numNeedles1 ; i++ ) {
		int32_t need = 1;
		// open.thumbshots.org needs multiple counts
		if ( i == 0 ) need = 5;
		if ( s_needles1[i].m_count < need ) continue;
		*note = s_needles1[i].m_string;
		return true;
	}

	// now check outlinks on the page for these substrings
	haystack     = links->getLinkBuf();
	haystackSize = links->getLinkBufLen();
	int32_t numNeedles2 = sizeof(s_needles2)/sizeof(Needle);
	getMatches2 ( s_needles2   , 
		      numNeedles2  , 
		      haystack     , 
		      haystackSize , 
		      NULL         ,  // linkPos, 
		      NULL         ,  // &n ,
		      false        ,  // stopAtFirstMatch?
		      NULL         ,  // hadPreMatch?
		      true         ,  // save quicktables
		      niceness     );

	QUICKPOLL( niceness );

	// see if we got a hit
	for ( int32_t i = 0 ; i < numNeedles2 ; i++ ) {
		int32_t need = 1;
		// open.thumbshots.org needs multiple counts
		//if ( i == 9 ) need = 5;
		if ( s_needles2[i].m_count < need ) continue;
		*note = s_needles2[i].m_string;
		return true;
	}

	QUICKPOLL( niceness );

	//skiplinks:
	// check for certain post tag, indicative of a comment-friendly blog
	// <form method=post ... action=*comments*cgi-bin>
	// <form method="post" 
	//       action="http://www.mydomain.com/cgi-bin/mt-comments.cgi" 
	//       name="comments_form" ...>
	// <form method=POST 
	//  action="http://peaceaction.org/wboard/wwwboard.cgi">
	int32_t nn = xml->getNumNodes();
	bool gotTextArea = false;
	bool gotSubmit   = false;
	for ( int32_t i=0; i < nn ; i++ ) {
		// <textarea> tags are bad... but only if we have not
		// matched "track" or whatever from above... check for that
		// if you uncommment this... otherwise you disable all blogs!
		// Only do this check if we did match a comment related phrase
		// in s_needles1[] BUT it was BEFORE our outlink. That 
		// basically means that we do *not* recognize the format of 
		// the comment page and so therefore need to be more 
		// restrictive about allowing this page to vote.
		if ( ! hadPreMatch ) {
			// is it a <textarea> tag?
			if ( xml->getNodeId ( i ) == TAG_TEXTAREA ) 
				gotTextArea = true;
			// is it an <input> tag?
			int32_t len = 0;
			if ( xml->getNodeId ( i ) == TAG_INPUT &&
			     xml->getString(i,"submit",&len)) gotSubmit = true;
		}
		// check for script tag
		/*
		if ( xml->getNodeId(i) == TAG_SCRIPT && quality < 80 ) {
			// <script src=blah.com/fileparse.js" 
			// type="text/javascript"> is used to hide google
			// ads, so don't allow those pages to vote either
			int32_t  slen; xml->getString(i,"src",&slen);
			if ( slen > 0 ) { *note = "script src"; return true; }
		}
		*/
		if ( xml->getNodeId ( i ) != TAG_FORM ) continue;
			
		// get the method field of this base tag
		int32_t  slen;
		char *s = (char *) xml->getString(i,"method",&slen);
		// if not thee, skip it
		if ( ! s || slen <= 0 ) continue;
		//if ( slen != 4 ) continue;
		// if not a post, skip it
		//if ( strncasecmp ( s , "post" , 4 ) ) continue;
		// get the action url
		s = (char *) xml->getString(i,"action",&slen);
		if ( ! s || slen <= 0 ) continue;
		char c = s[slen];
		s[slen]='\0';
		bool val = false;
		// this is a bit too strong, but i'ev seen an action of
		// "cgi-bin/mt-leaveone.cgi" so we can't rely on "mt-comment"
		if      ( strstr ( s , "comment" ) ) val = true;
		else if ( strstr ( s , "/MT/" ) ) val = true;
		else if ( strstr ( s , "/mt/" ) ) val = true;
		// they can have these search boxes though
		if ( val && strstr ( s , "/mt/mt-search" ) ) val = false;
		//else if ( strstr ( s , "cgi"     ) ) val = true;
		// eliminate some false positives
		//if ( val && strstr ( s , "search" ) ) val = false;
		s[slen] = c;
		if ( val ) { *note = "post page"; return true; }
	}

	if ( gotTextArea && gotSubmit ) {
		*note = "textarea tag";
		return true;
	}

	QUICKPOLL( niceness );

	// edu, gov, etc. can have link chains
	if ( tldLen >= 3 && strncmp ( tld, "edu" , 3) == 0 ) return false;
	if ( tldLen >= 3 && strncmp ( tld, "gov" , 3) == 0 ) return false;

	// if linker is naughty, he cannot vote
	if ( linker->isSpam() ) return true;

	// if being called from PageTitledb.cpp for displaying a titlerec, 
	// then do not call this, because no linkee is provided in that case.
	if ( !linkee ) {
		*note = "linkee not found";
		return false;//true;
	}

	// . allow sites in dmoz to have it too
	// . no, there are too many spam porn sites in dmoz
	int32_t *ids  = indCatIds;
	int32_t  nids = numIndCatIds;
	//if ( tr ) ids  = indCatIds;//tr->getIndCatids();
	//if ( tr ) nids = numIndCatids;//tr->getNumIndCatids();
	for ( int32_t j = 0 ; j < nids ; j++ ) 
		if ( g_categories && g_categories->isIdAdult ( ids[j] ) ) {
			*note = "dmoz porn"; 
			return true;
		}

	QUICKPOLL( niceness );
	// . if they link to any adult site, consider them link spam
	// . just consider a 100 link radius around linkNode
	int32_t nl = links->getNumLinks();
	int32_t linkNum =links->findLinkNum(linkee->getUrl(),linkee->getUrlLen());
	int32_t i0 = linkNum - 100;
	int32_t i1 = linkNum + 100;
	if ( i0 < 0  ) i0 = 0;
	if ( i1 > nl ) i1 = nl;
	for ( int32_t i = i0 ; i < i1 ; i++ ) {
		if ( ! g_categories ) continue;
		Url uu; uu.set ( links->getLink(i), links->getLinkLen(i) );
		char *h    = uu.getUrl();
		// include the trailing /
		char *hend = uu.getHost() + uu.getHostLen() + 1;
		uint32_t hhash = hash32 ( h , hend - h );
		if ( g_categories->isInBadCat ( hhash ) ) {
			*note = "links to dmoz filth";
			log(LOG_DEBUG,"build: %s is filthy.",uu.getUrl());
			return true;
		}
		QUICKPOLL( niceness );

		// . is it near sporny links? (naughty domains or lotsa -'s)
		// . if we are in a list of ads, chances are good the true
		//   nature of the ads will emerge...
		if ( i == linkNum      ) continue;
		if ( i - linkNum >=  4 ) continue;
		if ( i - linkNum <= -4 ) continue;
		if ( ! uu.isSpam()     ) continue;
		*note = "near sporny outlink";
		log(LOG_DEBUG,"build: %s is sporny.",uu.getUrl());
		return true;
	}


	// . allow sites in dmoz to have it too
	// . no, there are too many spam porn sites in dmoz
	//if ( tr->getNumIndCatids() || tr->getNumCatids()   ) return false;

	// init these before the loop
	int32_t  hlen  = linkee->getHostLen();
	char *host  = linkee->getHost();
	char *uu    = linkee->getUrl();
	char *uuend = host + hlen;
	int32_t  uulen = uuend - uu;
	int32_t  x     = linkNode;
 loop:

	QUICKPOLL( niceness );

	// return true right away if it is a link chain
	//if ( tr->getDocQuality() < 60 &&
	if ( siteNumInlinks < 1000 && 
	     isLinkChain ( xml , linker, linkee , x , note ) ) 
		return true;

	// if no domain, that's it
	if ( ! uu || uulen <= 0 ) return false;

	// . see if this domain is linked to in other areas of the document.
	// . if any of those areas are not link chains, then assume we are
	//   not a link chain
	for ( x++ ; x < nl ; x++ ) {
		char *link    = links->getLink    (x);
		int32_t  linkLen = links->getLinkLen (x);
		if ( ! link          ) continue;
		if ( linkLen <= 0    ) continue;
		if ( linkLen > uulen ) continue;
		if ( strncmp ( link , uu , uulen ) ) continue;
		// got a match, is it a link chain? if not, them we are not
		goto loop;
	}

	return false;

}

// Criteria for being a link chain:
//
// 1. the "linkee" is in a chain of outlinks to external domains
// 2. all outlinks to the same hostname as "linkee" are in link chains
// 3. no plain text is present between "linkee" and one of the other
//    outlinks in the chain
// 4. this might hurt blogrolls, and resource pages, but such links
//    are kind of low quality anyway.
bool isLinkChain ( Xml *xml , Url *linker , Url *linkee , int32_t linkNode ,
		   char **note ) {

	//log(LOG_DEBUG,"build: doing %s",linker->m_url);

	// if the linkee is internal (by domain) then not a link chain
	if ( linkee->m_dlen == linker->m_dlen &&
	     strncmp ( linkee->m_domain , linker->m_domain,linkee->m_dlen)==0)
		return false;

	char *linkPos = NULL;
	if ( linkNode >= 0 ) linkPos = xml->getNode ( linkNode );

	// did we have text to the left/right of this link and after/before
	// the neighboring link? assume not.
	bool leftText  = false;
	bool rightText = false;

	// the links on the left and right
	Url  leftUrl;
	Url  rightUrl;
	bool leftMalformed  = false;
	bool rightMalformed = false;
	// these do not have constructors so we must reset them
	leftUrl.reset();
	rightUrl.reset();
	int32_t i ;
	// . see if we are alone in a table or not
	// . table must occur before/after our left/right neighbor link
	bool tableLeft   = false;
	bool tableRight  = false;

	// going backwards from linkNode we are not in a link
	bool inLink = false;

	// get the start of an anchor tag on our immediate left
	for ( i = linkNode - 1 ; i >= 0 ; i-- ) {
		// do not look too far
		if ( linkPos - xml->getNode(i) >= 1500 ) break;
		if ( linkNode - i >= 90                ) break;
		// NOTE: if you add more tags to this list, then also add
		// to Vector::setPairHashes() as well
		// stop at <title> or </title> tags
		if ( xml->getNodeId(i) == TAG_TITLE ) break;
		// stop at <ul> or </ul> tags
		// no, otherwise, these lists are always "link chain left"
		//if ( xml->getNodeId(i) == TAG_UL ) break;
		// stop at <table> or </table> tags
		if ( xml->getNodeId(i) == TAG_TABLE ) {
			if ( ! xml->isBackTag(i) ) tableLeft = true;
			break;
		}

		// check for *plain* text
		if ( ! inLink && xml->getNodeId(i) == TAG_TEXTNODE ) {
			// get the node as a string
			char *p    = xml->getNode(i);
			char *pend = p + xml->getNodeLen(i);
			// check for elipsis, that is a sign that we are a serp
			for ( char *s = p ; s+2 < pend ; s++ ) {
				//if ( is_alnum(*s) ) break;
				if ( *s != '.' ) continue;
				s++;
				if ( *s != '.' ) continue;
				s++;
				if ( *s != '.' ) continue;
				// ok, got it
				*note = "search result right";
				return true;
			}
			// if we already got text, but searching still for ...
			if ( leftText ) continue;
			// does it have alnum
			if ( ! has_alpha_utf8 ( p , pend ) ) continue;
			leftText = true; 
			// do not break yet, cont search for ellipsis!
		}

		// keep chugging if not an anchor tag, <a> or </a>
		if ( xml->getNodeId(i) != TAG_A ) continue;
		// if we are </a> then we are now in a link since we are moving
		// backwards
		if ( xml->isBackTag(i) ) { inLink = true; continue; }
		// if we hit a forward tag and inLink was false... we had
		// no corresponding back tag, so disconsider any text
		if ( ! inLink ) rightText = false;
		// no longer in an <a> tag
		inLink = false;

		// ok, get the url from this anchor tag
		int32_t  ulen = 0;
		char *u = (char *) xml->getString ( i, "href", &ulen );
		// if we did not get one, that means it could have been
		// malformed... like the href had a quote right b4 it
		if ( ulen == 0 ) leftMalformed = true;
		// normalize
		if ( ulen > 0 ) 
			leftUrl.set ( linker , u , ulen );
		// . if NOT from the same domain, break out, otherwise continue
		// . this helps us find the <table> tag in ad tables with 
		//   multiple links to the same domain
		// . this helps us accept a list of links to the same domain if
		//   there is left/right text, like the guy that had a list
		//   to 3 different gigablast.com links in a row with no
		//   text in between
		if ( leftUrl.m_dlen != linkee->m_dlen  ) break;
		if ( strncmp ( leftUrl.m_domain ,
			       linkee->m_domain ,
			       linkee->m_dlen   )       ) break;
	}

	// we start off in link text, since linkNode is an <a> tag
	inLink = true;
	// now loop through all the nodes after us
	for ( i = linkNode + 1 ; i < xml->getNumNodes() ; i++ ) {
		// stop if we've gone too far
		if ( xml->getNode(i) - linkPos >= 1580 ) break;
		if ( i - linkNode >= 95                ) break;
		// stop at <title> or </title> tags
		if ( xml->getNodeId(i) == TAG_TITLE ) break;
		// stop at <table> or </table> tags
		if ( xml->getNodeId(i) == TAG_TABLE ) {
			// note it for table ads
			if ( xml->isBackTag(i) ) tableRight = true;
			break;
		}

		// check for *plain* text
		if ( ! inLink && xml->getNodeId(i) == TAG_TEXTNODE ) {
			// get the node as a string
			char *p    = xml->getNode(i);
			char *pend = p + xml->getNodeLen(i);
			// check for elipsis, that is a sign that we are a serp
			for ( char *s = p ; s+2 < pend ; s++ ) {
				//if ( is_alnum(*s) ) break;
				if ( *s != '.' ) continue;
				s++;
				if ( *s != '.' ) continue;
				s++;
				if ( *s != '.' ) continue;
				// ok, got it
				*note = "search result right";
				return true;
			}
			// if we already got text, but searching still for ...
			if ( rightText ) continue;
			// does it have alnum
			if ( ! has_alpha_utf8 ( p , pend ) ) continue;
			rightText = true; 
			// do not break yet, cont search for ellipsis!
		}

		// keep chugging if not an anchor tag, <a> or </a>
		if ( xml->getNodeId(i) != TAG_A ) continue;
		// skip if not a forward tag
		if ( xml->isBackTag(i) ) { inLink = false; continue; }
		// we are now in a link
		inLink = true;
		// stop text here
		//stopTextScan = i;
		// ok, get the url
		int32_t  ulen = 0;
		char *u = (char *) xml->getString ( i, "href", &ulen );
		// if we did not get one, that means it could have been
		// malformed... like the href had a quote right b4 it
		if ( ulen == 0 ) rightMalformed = true;
		// normalize
		if ( ulen > 0 ) 
			rightUrl.set ( linker, u , ulen );
		// . if NOT from the same domain, break out, otherwise continue
		// . this helps us find the <table> tag in ad tables with 
		//   multiple links to the same domain
		// . this helps us accept a list of links to the same domain if
		//   there is left/right text, like the guy that had a list
		//   to 3 different gigablast.com links in a row with no
		//   text in between
		if ( rightUrl.m_dlen != linkee->m_dlen  ) break;
		if ( strncmp ( rightUrl.m_domain ,
			       linkee->m_domain  ,
			       linkee->m_dlen    )       ) break;
	}

	if ( tableLeft && tableRight ) {
		*note = "ad table";
		return true;
	}

	// if we had text on both sides of us, we are not a link chain
	if ( leftText && rightText ) return false;

	if      ( ! leftText  && rightText ) *note = "link chain left";
	else if ( ! rightText && leftText  ) *note = "link chain right";
	else                                 *note = "link chain middle";

	return true;
}

/*
//we want to find the position of the linkee in the linker's xml class
//if it is in a series of links without vertical space in the end of
//the document, then we won't count it
bool isLinkSpam2 ( Url *linkee, TitleRec *tr, Xml *xml, Links *links ,
		   int32_t maxDocLen ) {
	int32_t linkNum = links->findLinkNum(linkee->getUrl(), 
					  linkee->getUrlLen());
	if(linkNum < 0) return false;

	int32_t thisXmlNode;
	int32_t nextXmlNode;
	int32_t numInARow = 1;
	int32_t needInARow = 3;

	int32_t thisLink = linkNum;
	int32_t nextLink = thisLink + 1;
	int16_t nodeId;
	int32_t i;
	int32_t len;
	char *c;
	char *cend;

 goRight:
	if(nextLink >= links->getNumLinks()) {
		//we can only go left from here.
		//log(LOG_WARN, "links: endotheline");
		goto goLeft;
	}
	thisXmlNode = links->getNodeNum(thisLink);
	nextXmlNode = links->getNodeNum(nextLink);
	 
	 //skip link text
	 for ( i=thisXmlNode; i < nextXmlNode ; i++ ) {
		 if(xml->isBackTag(i)) break;
	 }
	//could we find link text close tag?
	if(i == nextXmlNode) {
		//log(LOG_WARN, "links: couldn't find it");
		goto goLeft;
	}
	for (; i < nextXmlNode ; i++ ) {
		nodeId = xml->getNodeId(i);
		if(nodeId == TAG_BR  ||//br
		   nodeId == TAG_TR ||//tr
		   nodeId == TAG_LI) {//li
			//log(LOG_WARN, "links: breaking tag");
			goto goLeft;
		}
		//do we have a rel="nofollow" link which isn't in the 
		//links class.  If so, skip past it's link text.
		if(nodeId == TAG_A) {
			for ( ; i < nextXmlNode ; i++ ) {
				if(xml->isBackTag(i)) break;
			}
			if(i == nextXmlNode) goto goLeft;
		}

		if(nodeId != TAG_TEXTNODE && nodeId != TAG_B) continue;
		c = xml->getNode(i); 
		len = xml->getNodeLen(i); 
		cend  = c + len;

		while (c < cend) {
			if(*c == '&') 
				while (c < cend && *c != ';') c++;
			if(is_alnum_utf8(c)) {
				//log(LOG_WARN, "links: stupid %c", *c);
				goto goLeft;
			}
			c++;
		}
	}
	

	//ok, so we got to the next link without
	//breaking, try getting the next link
	numInARow++;
	if(numInARow >= needInARow) goto goLeft;

	thisLink++;
	nextLink++;
	goto goRight; 

 goLeft:
	//log(LOG_WARN, "links: numinarow %"INT32" linkee %s , linker %s",
	//numInARow, linkee->getUrl(), tr->getUrl()->getUrl());
	if(numInARow >= needInARow) return true;

	//get out if we've already tried going left
	if(thisLink < linkNum) return false;

	needInARow = needInARow - numInARow;
	thisLink = linkNum - needInARow;
	nextLink = thisLink + 1;
	if(thisLink < 0) return false;
	goto goRight;


 	return false;
}
*/

static Needle s_needles3[] = {
	// this often directly precedes the comment section
	{"[trackback"                   , 0 , 1 , 1 , 0 , NULL , 0 , NULL } ,
	{"class=\"comtext"              , 0 , 8 , 1 , 0 , NULL , 0 , NULL } ,
	{"class=\"comment"              , 0 , 8 , 1 , 0 , NULL , 0 , NULL } ,
	{"class=\"coment"               , 0 , 8 , 1 , 0 , NULL , 0 , NULL } ,
	{"class=\"trackback"            , 0 , 8 , 1 , 0 , NULL , 0 , NULL } ,
	{"class=\"ping"                 , 0 , 8 , 1 , 0 , NULL , 0 , NULL } ,
	{"class=\"followup"             , 0 , 8 , 1 , 0 , NULL , 0 , NULL } ,
	{"class=\"response"             , 0 , 8 , 1 , 0 , NULL , 0 , NULL } ,
	// this can signify a blog entry, not just a comment
	//{"class=\"entry"              , 0 , 8 , 1 , 0 , NULL , 0 , NULL } ,
	// these seem to be more indicative of posted comments
	{"class=\"posted"               , 0 , 8 , 1 , 0 , NULL , 0 },
	{"id=\"posted"                  , 0 , 8 , 1 , 0 , NULL , 0 },
	{"name=\"posted"                , 0 , 8 , 1 , 0 , NULL , 0 },
	// annoying little textbox thingy
	{"class=\"shoutbox"             , 0 , 8 , 1 , 0 , NULL , 0 , NULL } ,
	{"id=\"comment"                 , 0 , 8 , 1 , 0 , NULL , 0 , NULL } ,
	{"id=\"coment"                  , 0 , 8 , 1 , 0 , NULL , 0 , NULL } ,
	{"id=\"trackback"               , 0 , 8 , 1 , 0 , NULL , 0 , NULL } ,
	{"id=\"ping"                    , 0 , 8 , 1 , 0 , NULL , 0 , NULL } ,
	{"id=\"followup"                , 0 , 8 , 1 , 0 , NULL , 0 , NULL } ,
	{"id=\"response"                , 0 , 8 , 1 , 0 , NULL , 0 , NULL } ,
	{"name=\"comment"               , 0 , 8 , 1 , 0 , NULL , 0 , NULL } ,
	{"name=\"coment"                , 0 , 8 , 1 , 0 , NULL , 0 , NULL } ,
	{"name=\"trackback"             , 0 , 8 , 1 , 0 , NULL , 0 , NULL } ,
	{"name=\"ping"                  , 0 , 8 , 1 , 0 , NULL , 0 , NULL } ,
	{"name=\"followup"              , 0 , 8 , 1 , 0 , NULL , 0 , NULL } ,
	{"name=\"response"              , 0 , 8 , 1 , 0 , NULL , 0 , NULL } ,
	// message boards
	{"anonymous user"               , 0 , 10, 1 , 0 , NULL , 0 , NULL } ,
	{"anonymer user"                , 0 , 10, 1 , 0 , NULL , 0 , NULL } ,
	{"date posted"                  , 0 , 10, 1 , 0 , NULL , 0 , NULL } ,
	{"post your notice"             , 0 , 10, 1 , 0 , NULL , 0 , NULL } ,
	//{"edit this page"               , 0 , 10, 1 , 0 , NULL , 0 , NULL } ,
	// edit</a><br>
	{"edit<a]br"                    , 0 , 10, 1 , 0 , NULL , 0 , NULL } ,
	// link to edit a comment
	{">edit</a"                     , 0 , 10, 1 , 0 , NULL , 0 , NULL } ,
	// these often indicate blog entries, not just comments
	//{"reply with quote"             , 0 , 9 , 0 , 0 , NULL , 0 , NULL } ,
	//{">post a reply"                , 0 , 10, 0 , 0 , NULL , 0 , NULL } ,
	//{"post reply"                   , 0 , 10, 0 , 0 , NULL , 0 , NULL } ,
	//{"submit post"                  , 0 , 10, 0 , 0 , NULL , 0 , NULL } ,
	//{">post message"                , 0 , 10, 0 , 0 , NULL , 0 , NULL } ,
	//{">post a comment"              , 0 , 10, 0 , 0 , NULL , 0 , NULL } ,
	//{">leave a comment"             , 0 , 10, 0 , 0 , NULL , 0 , NULL } ,
	//{">post comments"               , 0 , 10, 0 , 0 , NULL , 0 , NULL } ,
	// Comments</font> (0) after each posted entry...
	//{">comments<"                 , 0 , 10, 1 , 0 , NULL , 0 , NULL } ,
	{"comments: <"                  , 0 , 10, 1 , 0 , NULL , 0 , NULL } ,
	{"comments:<"                   , 0 , 10, 1 , 0 , NULL , 0 , NULL } ,
	//{"comment:"                   , 0 , 10, 1 , 0 , NULL , 0 , NULL } ,
	{"reacties:"                    , 0 , 10, 1 , 0 , NULL , 0 , NULL } ,
	{"comentarios:"                 , 0 , 10, 1 , 0 , NULL , 0 , NULL } ,
	{"comentários:"                 , 0 , 10, 1 , 0 , NULL , 0 , NULL } ,
	{">message:"                    , 0 , 10, 0 , 0 , NULL , 0 , NULL } ,
	{">mensagem:"                   , 0 , 10, 0 , 0 , NULL , 0 , NULL } ,
	{">faca seu comentario"         , 0 , 10, 0 , 0 , NULL , 0 , NULL } ,
	{">faça seu comentário"         , 0 , 10, 0 , 0 , NULL , 0 , NULL } ,
	// comment add in german
	{">Kommentar hinzuf"            , 0 , 10, 0 , 0 , NULL , 0 , NULL } ,
	{">add my comment"              , 0 , 10, 0 , 0 , NULL , 0 , NULL } ,
	// title of the text area box
	{">your comment"                , 0 , 10, 0 , 0 , NULL , 0 , NULL } ,
	{"your comment<"                , 0 , 10, 0 , 0 , NULL , 0 , NULL } ,
	{">comment by"                  , 0 , 10, 1 , 0 , NULL , 0 , NULL } ,
	{">scrivi un commento"          , 0 , 10, 0 , 0 , NULL , 0 , NULL } ,
	{">scrivi il tuo commento"      , 0 , 10, 0 , 0 , NULL , 0 , NULL } ,
	{"add comment"                  , 0 , 10, 0 , 0 , NULL , 0 , NULL } ,
	{"trackbacks for the art"       , 0 , 12, 1 , 0 , NULL , 0 , NULL } ,
	{"these trackbacks have been re", 0 , 13, 1 , 0 , NULL , 0 , NULL } ,
	{"trackback pings"              , 0 , 13, 1 , 0 , NULL , 0 , NULL } ,
	{"read the rest of this com"    , 0 , 13, 1 , 0 , NULL , 0 , NULL } ,
	// that was the opinion of ...
	{"das war die meinung von"      , 0 , 13, 1 , 0 , NULL , 0 , NULL } ,
	//{"add new comment"              , 0 , 14, 0 , 0 , NULL , 0 },
	//{"add message"                  , 0 , 14, 0 , 0 , NULL , 0 },
	// tagboard software allows free submits. it has this in 
	// an html comment tag...
	{"2002 natali ardianto"         , 0 , 14, 0 , 0 , NULL , 0 , NULL } ,
	// guestbooks
	//{"guestbook</title"             , 0 , 13, 0 , 0 , NULL , 0 , NULL } ,
	//{"gastenboek</title"            , 0 , 13, 0 , 0 , NULL , 0 , NULL } ,
};

// gives you where the comment section starts
// looks for only the first comment section
char *getCommentSection ( char *haystack     ,
			  int32_t  haystackSize ,
			  int32_t  niceness     ){

	// get our page quality, it serves as a threshold for some algos
	//char quality = tr->getNewQuality();

	//char *linkPos = NULL;
	//if ( linkNode >= 0 ) linkPos = xml->getNode ( linkNode );

	//if ( strstr ( linker->getUrl() , "usa_apartments1.htm") ) {
	//	log("hey");
	//	sleep(7);
	//}

	// loop:
	// do not call them "bad links" if our link occurs before any
	// comment section. our link's position therefore needs to be known,
	// that is why we pass in linkPos. 
	// "n" is the number it matches.
	//int32_t numNeedles1 = sizeof(s_needles3)/sizeof(Needle);
	return getMatches2 ( s_needles3  ,
			     sizeof(s_needles3)/sizeof(Needle),
			     haystack    ,
			     haystackSize,
			     NULL        ,
			     NULL        ,
			     true        ,// stopAtFirstMatch
			     NULL        , // hadPreMatch?
			     false       , // save quick tables?
			     niceness    );
}
