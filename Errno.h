// Matt Wells, copyright Mar 2001

// . extensions of errno
// . have 16th bit set to avoid collisions with existing errnos

#ifndef _MYERRNO_H_
#define _MYERRNO_H_

// use our own errno so threads don't fuck with it
extern int g_errno;

char *mstrerrno ( int errnum ) ;
char *mstrerror ( int errnum ) ;

// . this is OR'ed into the errno if the errno occured on a remote machine but
//   was passed back to use through a reply (see UdpSlot.cpp/UdpServer.cpp)
// . this was removed because Multicast::gotReply1() was expecting the g_errno
//   remote codes to be the same as local, like ENOTFOUND to be right!!
//#define REMOTE_ERROR_BIT (0x40000000)

enum {
	EDUMPFAILED    = (0x00008000 | 0) , // tree dump failed 32768
	ETRYAGAIN        , // try doing it again
	ECLOSING         , // can't add cuz we're closing the db
	ENOTFOUND        , // can't find in the db
	EHOSTNAMETOOBIG  , // hostname too big
	EOUTOFSOCKETS    , // no more sockets?
	EURLTOOBIG       , // too many chars in url
	ENOSITEDEFAULT   , // tagdb record's default record missng
	EBADREPLYSIZE    , // reply is wrong length
	EBADREPLY        , // something is wrong w/ reply
	EREPLYTOOSMALL   , // reply is too small  32778
	EREQUESTTOOSHORT , // request length too int16_t
	EBADREQUESTSIZE  , // request length not correct 32780
	EBADREQUEST      , // a bad request
	ENOTSUPPORTED    , // operation not yet supported
	EBADHOSTID       , // someone tried to use a bad hostId
	EBADENGINEER     , // me being lazy
	EISCLOSING       , // cannot add because db is closing
	EDATANOTOWNED    , // trying to write on data you don't own
	EDATAUNPATCHABLE , // got unpatchable corrupt data
	EBADRDBID        , // bad rdb id
	EBUFTOOSMALL     , // used in SiteRec.cpp
	ECOMPRESSFAILED  , // used in TitleRec.cpp  32790
	EUNCOMPRESSERROR , // used in TitleRec.cpp
	EBADTITLEREC     , // used in TitleRec.cpp
	EMISSINGQUERYTERMS,// used in Msg20.cpp for getting summary
	EBADLIST         , // used in titledb/Msg23.cpp
	ENODOCID         , // used in titledb/Msg24.cpp
	ENOHOSTS         , // multicast can't find any hosts
	ENOSLOTS         , // multicast can't use more than X slots
	ENOTHREADSLOTS   , // no more room in thread queue
	EBADNUMHOSTS     , // hostdb error
	EFILEOPEN        , // error opening/reading a tagdb file  32800
	EURLTOOLONG      , 
	EDOCBINARY       , //parser/xml/XmlDoc.cpp
	EDOCADULT        , //parser/xml/XmlDoc.cpp
	EDOCBANNED       , 
	EDOCFORCEDELETE  , // doc force deleted
	EDOCURLSPAM      , // url detected as spam/porn
	EDOCSPAM         , // the doc is spam
	EDOCLINKBANNED   , // banned cuz linker as <linksBanned>1</> in rs
	EDOCCGI          , 
	EDOCURLIP        , 
	EDOCBADCONTENTTYPE   ,
	EDOCQUALITYLOW       , 
	EDOCBADHTTPSTATUS    , 
	EDOCREDIRECTSTOSELF  , 
	EDOCTOOMANYREDIRECTS ,
	EDOCSIMPLIFIEDREDIR  , 
	EDOCBADREDIRECTURL   , 
	EDOCTOOBIG       ,  
	EDOCTOOSMALL     ,  
	EDOCTOOOLD       ,  
	EDOCTOONEW       ,  
	//EDOCNOTNEW       ,  
	//EDOCNOTOLD       ,  
	EDOCNOTMODIFIED  ,  
	EDOCUNCHANGED    ,
	EDOCUNCHANGED2   ,
	EDOCDUP          ,  
	EDOCDUPWWW       ,  
	EDOCQUOTABREACH  , //breached the max docs quota
	EDOCDISALLOWED   , //robots.txt disallows this url
	EDOCNOINDEX      , //meta robots tag says not to index
	EDOCNOINDEX2     , //ruleset (tagdb*.xml)has <indexDoc>no</> in it
	EDOCASIAN        , //asian charset disallowed
	EDOCWRONGIP      , //doc has wrong ip, does not match sanityIp
	EDOCNODOLLAR     , //doc does not have a $ sign, but needs it
	EDOCNONUMBERS    , //doc does not have two back-to-back numbers in url
	EDOCHASRSSFEED   , //doc has an rss feed to be followed
	EDOCNOTRSS       , //doc is not linked to by RSS as required
	EDOCISANCHORRSS  , //doc's rss links uses anchors to one page
	EDOCHASBADRSS    , //doc is pointed to by a bad RSS
	EDOCISSERP       , //doc is a search results page
	ETOOMANYLISTS    , //used by rdb/Msg2.cpp for getting lists
	ETOOMANYFILES    , //used by Rdb class when trying to dump
	EQUERYTOOBIG     , //used by parser/query/SimpleQuery.cpp
	EQUERYTRUNCATED  , //used in Msg39.cpp
	ETOOMANYOPERANDS , //used in Query.cpp
	ENOTLOCAL        , //docId is not local (titledb/Msg20.cpp)
	ETCPTIMEDOUT     , //op timed out TcpServer.cpp
	EUDPTIMEDOUT     , //udp reply timed out
	ESOCKETCLOSED    , //device disconnected (POLL_HUP) Loop.cpp
	EBADMIME         , //Mime.cpp
	ENOHOSTSFILE     , //Hostdb::init() needs a hosts file
	ENOHOSTIP        , //host file missing an IP entry for a host
	EURLHASNOIP      , //parser/url/Url2.cpp::hashIp()
	EBADIP           , //parser/url/Url2.cpp::hashIp()
	EMSGTOOBIG       , //msg is too big
	EDNSBAD          , //dns sent us a wierd response code
	EDNSREFUSED      , //dns refused to talk to us
	EDNSDEAD         , //dns is dead
	EDNSTIMEDOUT     , //was just EUDPTIMEDOUT
	ECOLLTOOBIG      , //collection is too long
	ESTRIKEOUT       , //retried enough times; deleting doc & giving up
	ENOPERM          , //permission denied
	ECORRUPTDATA     , //corrupt data
	ENOCOLLREC       , //no collection record
	ESHUTTINGDOWN    , //shutting down the server
	EHOSTDEAD        , // host is dead
	EBADFILE         , //file is bad
	ETOOEARLY        , //need to wait longer
	EFILECLOSED      , //read on closed file?
	ELISTTOOBIG      , //Rdb::addList() calls this
	ECANCELLED       , //transaction was cancelled
	//EHAMMERIP        , //downloading page would hammer ip
	//EHAMMERDOMAIN    , //downloading page would hammer domain
	EDOCLANG         , // doc written in an invalid language
	EBUYFEED         , //IP has exceeded free usage quota.
	EBADCHARSET      , // Unsupported charset
	ETOOMANYDOWNLOADS, //too many concurrent http downloads
	EBADPROXY        , //admin request to a proxy 
	ELINKLOOP        , //url is repeating path components in a loop
	ENOCACHE         , // document disallows caching
	EREPAIRING       , // we are in repair mode, cannot add data
	ECANCELACK       , // read a cancel ack, destroy the slot
	EBADURL          ,
	EDOCFILTERED     , // doc is filtered
	ESSLNOTREADY     , // SSl tcpserver is not ready to do HTTPS request
	ERESTRICTEDPAGE  , // spider trying to download /master or /admin page
	//ESPIDERRECDUP    , // duplicate spiderdb record
	EDOCISERRPG      , // Doc is error page
	EFORCED          , // Doc was force re-spidered
	EINJECTIONSDISABLED        , // injection is disabled
	ETAGBREACH       , // Sections.cpp ran out of stack space
	EDISKSTUCK       ,
	EDOCHIJACKED     ,
	EDOCREPEATSPAMMER,
	EDOCEVILREDIRECT ,
	EDOCBADSECTIONS  ,
	EDOCBADDATES     ,
	EBADGEOCODERREPLY,
	EBUFOVERFLOW     ,
	EPLSRESUBMIT     ,
	EURLBADYEAR      ,
	EABANDONED       ,
	ECORRUPTHTTPGZIP ,
	EDOCIDCOLLISION  ,
	ESSLERROR        ,
	EPERMDENIED      ,
	ENOFUNDS         ,
	EDIFFBOTINTERNALERROR,
	EDIFFBOTMIMEERROR,
	EDIFFBOTBADHTTPSTATUS,
	EHITCRAWLLIMIT,
	EHITPROCESSLIMIT,
	EINTERNALERROR,
	EBADJSONPARSER,
	EFAKEFIRSTIP,
	EBADHOSTSCONF,
	EWAITINGTOSYNCHOSTSCONF,
	EDOCNONCANONICAL,
	ECUSTOMCRAWLMISMATCH, // a crawl request was made with a name that already existed for bulk request (or the other way around)
	ENOTOKEN,
	EBADIMG,
	EREINDEXREDIR,
	ETOOMANYPARENS,

	EDIFFBOTUNABLETOAPPLYRULES,
	EDIFFBOTCOULDNOTPARSE,
	EDIFFBOTCOULDNOTDOWNLOAD,
	EDIFFBOTINVALIDAPI,
	EDIFFBOTVERSIONREQ,
	EDIFFBOTEMPTYCONTENT,
	EDIFFBOTREQUESTTIMEDOUT,
	EDIFFBOTURLPROCESSERROR,
	EDIFFBOTTOKENEXPIRED,
	EDIFFBOTUNKNOWNERROR,

	EMISSINGINPUT,
	EDMOZNOTREADY,
	EPROXYSSLCONNECTFAILED,
	EINLINESECTIONS,
	EREADONLYMODE,
	ENOTITLEREC,
	EQUERYINGDISABLED,
	EJSONMISSINGLASTCURLY,
	EADMININTERFERENCE,
	EDNSERROR        ,
	ETHREADSDISABLED,
	EMALFORMEDQUERY,
	ESHARDDOWN,
	EDOCWARC,
	EWRONGSHARD,
	EDIFFBOTREQUESTTIMEDOUTTHIRDPARTY,
	EDIFFBOTTOOMANYTEXTNODES,
	EDIFFBOTCURLYREPLY,
	EDIFFBOTTOKENUNAUTHORIZED,
	EDIFFBOTPLAINERROR
};
#endif
