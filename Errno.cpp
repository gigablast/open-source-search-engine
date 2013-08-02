#include "gb-include.h"

#include "Errno.h"

// use our own errno so threads don't fuck with it
int g_errno;

char *mstrerror ( int errnum ) {
	return mstrerrno ( errnum );
}

char *mstrerrno ( int errnum ) {

	switch ( errnum ) {

case 	EDUMPFAILED      : return "Tree dump failed";
case 	ETRYAGAIN        : return "Try doing it again";
case 	ECLOSING         : return "Add denied, db is closing";
case 	ENOTFOUND        : return "Record not found";
case 	EHOSTNAMETOOBIG  : return "Hostname too big";
case 	EOUTOFSOCKETS    : return "No more sockets";
case 	EURLTOOBIG       : return "Too many chars in url";
case 	ENOSITEDEFAULT   : return "Could not get the default tagdb record";
case 	EBADREPLYSIZE    : return "Reply is wrong length";
case 	EBADREPLY        : return "Something is wrong with reply";
case 	EREPLYTOOSMALL   : return "Reply is too small";
case 	EREQUESTTOOSHORT : return "Request length too short";
case 	EBADREQUESTSIZE  : return "Request length not correct";
case 	EBADREQUEST      : return "Bad request";
case 	ENOTSUPPORTED    : return "Operation not yet supported";
case 	EBADHOSTID       : return "Someone tried to use a bad hostId";
case 	EBADENGINEER     : return "Bad engineer";
case 	EISCLOSING       : return "Can not add because db is closing";
case 	EDATANOTOWNED    : return "Trying to write on data not owned";
case	EDATAUNPATCHABLE : return "Got corrupt data that cannot be patched";
case 	EBADRDBID        : return "Bad Rdb id";
case 	EBUFTOOSMALL     : return "Buf too small";
case 	ECOMPRESSFAILED  : return "Compress failed";
case 	EUNCOMPRESSERROR : return "Uncompress failed";
case 	EBADTITLEREC     : return "Bad cached document";
case	EMISSINGQUERYTERMS:return "Document is missing query terms";
case 	EBADLIST         : return "Bad list";
case 	ENODOCID         : return "No docid";
case 	ENOHOSTS         : return "Multicast can not find any hosts";
case 	ENOSLOTS         : return "No udp slots available";
case	ENOTHREADSLOTS   : return "No room for thread in thread queue";
case 	EBADNUMHOSTS     : return "Hostdb error";
case 	EFILEOPEN        : return "Error opening or reading a file in Sitedb";
case 	EURLTOOLONG      : return "Url too long";
case 	EDOCBINARY       : return "Doc binary";
case 	EDOCADULT        : return "Doc adult";
case 	EDOCBANNED       : return "Doc banned";
case 	EDOCFORCEDELETE  : return "Doc force deleted";
case	EDOCURLSPAM      : return "Url detected as spam or porn";
case	EDOCSPAM         : return "Doc detected as spam";
case	EDOCLINKBANNED   : return "Doc is link banned";
case    EDOCCGI          : return "Doc is CGI";
case	EDOCURLIP        : return "Doc url is IP based";
case    EDOCBADCONTENTTYPE : return "Doc bad content type";
case 	EDOCQUALITYLOW   : return "Doc quality too low";
case 	EDOCBADHTTPSTATUS : return "Doc bad http status";
case	EDOCREDIRECTSTOSELF:return "Doc redirects to self";
case 	EDOCTOOMANYREDIRECTS: return "Doc redirected too much";
case	EDOCSIMPLIFIEDREDIR : return "Doc redirected to simpler url";
case	EDOCBADREDIRECTURL  : return "Doc bad redirect url";
case 	EDOCTOOBIG       : return "Doc too big";
case 	EDOCTOOSMALL     : return "Doc too small";
case 	EDOCTOOOLD       : return "Doc too old";
case 	EDOCTOONEW       : return "Doc too new";
//case 	EDOCNOTNEW       : return "Doc not new";
//case 	EDOCNOTOLD       : return "Doc not old";
case	EDOCNOTMODIFIED  : return "Doc not modified";
case	EDOCUNCHANGED    : return "Doc unchanged";
case	EDOCUNCHANGED2   : return "Doc pretty much unchanged";
case	EDOCDUP          : return "Doc is a dup";
case	EDOCDUPWWW       : return "Doc is dup of a www url";
case	EDOCQUOTABREACH  : return "Doc would breach the page quota";
case	EDOCDISALLOWED   : return "robots.txt disallows this url";
case	EDOCNOINDEX      : return "Meta robots tag says not to index";
case	EDOCNOINDEX2     : return "Sitedb or url filters prohibit indexing";
case	EDOCASIAN        : return "Asian charset disallowed";
case	EDOCWRONGIP      : return "Doc has wrong IP";
case	EDOCNODOLLAR     : return "Doc has no dollar sign";
case	EDOCNONUMBERS    : return "Doc does not have double digits in url";
case	EDOCHASRSSFEED   : return "Doc has an RSS to be followed";
case	EDOCNOTRSS       : return "Doc not linked to by RSS as required";
case	EDOCISANCHORRSS  : return "Doc's RSS uses relative anchors "
				   "(pound sign)";
case	EDOCHASBADRSS    : return "Doc is linked to by RSS with bad format";
case	EDOCISSERP       : return "Doc is a search results page";
case 	ETOOMANYLISTS    : return "Too many lists";
case	ETOOMANYFILES    : return "Too many files already";
case 	EQUERYTOOBIG     : return "Query too big";
case 	EQUERYTRUNCATED  : return "Query was truncated";
case 	ETOOMANYOPERANDS : return "Boolean query has too many operands";
case 	ENOTLOCAL        : return "DocId is not local";
case 	ETCPTIMEDOUT     : return "Tcp operation timed out";
case	EUDPTIMEDOUT     : return "Udp reply timed out";
case 	ESOCKETCLOSED    : return "Device disconnected (POLL_HUP)";
case 	EBADMIME         : return "Bad mime";
case 	ENOHOSTSFILE     : return "No hosts.conf file";
case 	ENOHOSTIP        : return "hosts.conf file missing an IP entry for a "
				   "host";
case 	EURLHASNOIP      : return "Url has no IP";
case 	EBADIP           : return "Bad IP";
case	EMSGTOOBIG       : return "Msg is too big";
case	EDNSBAD          : return "DNS sent an unknown response code";
case	EDNSREFUSED      : return "DNS refused to talk";
case	EDNSDEAD         : return "DNS hostname does not exist";
case	EDNSTIMEDOUT     : return "DNS timed out";
case	ECOLLTOOBIG      : return "Collection is too long";
case	ESTRIKEOUT       : return "Retried enough times, deleting doc";
case	ENOPERM          : return "Permission Denied";
case	ECORRUPTDATA     : return "Corrupt data";
case    ENOCOLLREC       : return "No collection record";
case	ESHUTTINGDOWN    : return "Shutting down the server";
case    EHOSTDEAD        : return "Host is marked as dead";
case	EBADFILE         : return "File is bad";
case	ETOOEARLY        : return "Need to wait longer";
case	EFILECLOSED      : return "Read on closed file";//close on our thread
case	ELISTTOOBIG      : return "List is too big";
case	ECANCELLED       : return "Transaction was cancelled";
//case	EHAMMERIP        : return "Downloading page would hammer IP";
//case	EHAMMERDOMAIN    : return "Downloading page would hammer domain";
case    EDOCLANG         : return "Document is wrong language";
case    EBUYFEED         : return "Contact us to buy a search feed";
case    EBADCHARSET      : return "Unsupported charset";
case	ETOOMANYDOWNLOADS : return "Too many outstanding http downloads";
case    EBADPROXY        : return "Admin request not supported by proxy";
case    ELINKLOOP        : return "Url is repeating path components";
case    ENOCACHE         : return "Page disallows caching";
case    EREPAIRING       : return "Can not add data to host in repair mode";
case	ECANCELACK       : return "Read a cancel ack, destroy the slot";
case	EBADURL          : return "Malformed url";
case	EDOCFILTERED     : return "Doc is filtered"; // like banned...
case    ESSLNOTREADY     : return "SSL tcpserver not ready";
case	ERESTRICTEDPAGE  : return "Page is /admin or /master and restricted";
//case	ESPIDERRECDUP    : return "Duplicate spiderdb record";
case	EDOCISERRPG      : return "Doc is error page";
case	EFORCED          : return "Doc was force respidered";
case	EDISABLED        : return "Injection is disabled in Master Controls";
case	ETAGBREACH       : return "Sections parser ran out of tag stack space";
case	EDISKSTUCK       : return "Disk is stuck";
case	EDOCHIJACKED     : return "Doc is hijacked";
case    EDOCREPEATSPAMMER: return "Doc is repetitive spam";
case	EDOCEVILREDIRECT : return "Doc evil redirect url";
case	EDOCBADSECTIONS  : return "Doc has malformed sections";
case	EDOCBADDATES     : return "Doc has malformed dates or dates overflow";
case	EBADGEOCODERREPLY: return "Geocoder returned bad reply or timed out";
case	EBUFOVERFLOW     : return "Static buffer overflow";
case	EPLSRESUBMIT     : return "The system was restarted. Please resubmit your evaluation.";
case	EURLBADYEAR      : return "Url contained an out of usable range year";
case	EABANDONED       : return "Injection is abandoned";
case	ECORRUPTHTTPGZIP : return "Http server returned corrupted gzip";
case	EDOCIDCOLLISION  : return "DocId collision in titledb";
case	ESSLERROR        : return "SSL error of some kind";
case    EPERMDENIED      : return "Permission denied";
case    ENOFUNDS         : return "Not enough funds in account";
	}
	// if the remote error bit is clear it must be a regulare errno
	//if ( ! ( errnum & REMOTE_ERROR_BIT ) ) return strerror ( errnum );
	// otherwise, try it with it cleared
	//return mstrerrno ( errnum & (~REMOTE_ERROR_BIT) );
	return strerror ( errnum );
}
