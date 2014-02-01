#include "gb-include.h"

#include "Xml.h"

#include "Mem.h"     // mfree(), mmalloc()
#include "Unicode.h" // for html entities that return unicode
#include "Titledb.h"
#include "Words.h"
//#include "DateParse2.h"

Xml::Xml  () { 
	m_xml = NULL; 
	m_xmlLen = 0; 
	m_nodes = NULL; 
	m_numNodes=0; 
	m_ownData = false;
	m_version = TITLEREC_CURRENT_VERSION;
}

// . should free m_xml if m_copy is true
Xml::~Xml () { 
	reset(); 
}

// . for parsing xml conf files
bool Xml::getBool ( long n0 , long n1 , char *tagName , bool defaultBool ) {
	long len;
	char *s = getTextForXmlTag ( n0 , n1 , tagName , &len , true );
	if ( s ) return atob ( s , len );
	// return the default if no non-white-space text
	return defaultBool;
}

// . for parsing xml conf files
long Xml::getLong ( long n0 , long n1 , char *tagName , long defaultLong ) {
	long len;
	char *s = getTextForXmlTag ( n0 , n1 , tagName , &len , false );
	if ( s ) return atol2 ( s , len );
	// return the default if no non-white-space text
	return defaultLong;
}

// . for parsing xml conf files
long long Xml::getLongLong ( long n0 , long n1 , char *tagName , 
			     long long defaultLongLong         ) {
	long len;
	char *s = getTextForXmlTag ( n0 , n1 , tagName , &len , false );
	if ( s ) return atoll2 ( s , len );
	// return the default if no non-white-space text
	return defaultLongLong;
}

// . for parsing xml conf files
float Xml::getFloat (long n0 , long n1 , char *tagName,float defaultFloat){
	long len;
	char *s = getTextForXmlTag ( n0 , n1 , tagName , &len , false );
	if ( s ) return atof2 ( s , len );
	// return the default if no non-white-space text
	return defaultFloat;
}

char *Xml::getString ( long n0 , long n1 , char *tagName, long *len ,
		       bool skipLeadingSpaces ) const {
	char *s = getTextForXmlTag ( n0, n1, tagName, len, skipLeadingSpaces );
	if ( s ) return s;
	// return the default if s is null
	return NULL;
}

// . used by getValueAsBool/Long/String()
// . tagName is compound for xml tags, simple for html tags
// . NOTE: we skip over leading spaces
char *Xml::getTextForXmlTag ( long n0 , long n1 , char *tagName , long *len ,
			      bool skipLeadingSpaces ) const {
	// assume len is 0
	*len = 0;
	// get a matching xml TAG
	long num = getNodeNum ( n0 , n1 , tagName , gbstrlen(tagName) );
	if ( num < 0                 ) return NULL;
	return getString ( num , skipLeadingSpaces , len );
}


char *Xml::getString ( long num , bool skipLeadingSpaces , long *len ) const {
	// get the text of this tag (if any)
	if ( ++num >= m_numNodes     ) { *len = 0; return NULL; }
	if ( ! m_nodes[num].isText() ) { *len = 0; return NULL; }
	// if we don't skip leading spaces return it as is
	if ( ! skipLeadingSpaces ) {
		*len   = m_nodes[num].m_nodeLen;		
		return   m_nodes[num].m_node;
	}

	// get the string
	char *s    = m_nodes[num].m_node;
	// set the length and return the string
	long  slen = m_nodes[num].m_nodeLen;
	// skip leading spaces
	while ( is_wspace_utf8 ( s ) && slen > 0 ) { s++; slen--; }
	// set len
	*len = slen;
	// return NULL if slen is 0
	if ( slen == 0 ) return NULL;
	// otherwise return s
	return s;
}

char *Xml::getNode ( char *tagName , long *len ) {
	// assume len is 0
	*len = 0;
	// get a matching xml TAG
	long num = getNodeNum ( 0 , m_numNodes, tagName , gbstrlen(tagName) );
	if ( num < 0                 ) return NULL;

	// no back tag if its like <languages/> it won't have one
	XmlNode *node = &m_nodes[num];
	if ( ! node->m_hasBackTag ) return NULL;

	// scan for ending back tag
	long i ; for ( i = num + 1 ; i < m_numNodes ; i++ ) {
		if ( m_nodes[i].m_hash != node->m_hash ) continue;
		break;
	}
	if ( i >= m_numNodes ) return NULL;

	// got the back tag
	char *end = m_nodes[i].m_node;
	char *s = m_nodes[num+1].m_node;

	// trim spaces
	while ( s < end && is_wspace_a ( *s ) ) s++;
	while ( end-1 > s && is_wspace_a ( end[-1] ) ) end--;

	*len = end - s;
	return s;
}



long long Xml::getCompoundHash ( char *s , long len ) const {
	// setup
	char *p     = s;
	char *start = s;
	long i   = 0;
	long long h = 0;
 loop:
	// find fisrt .
	while ( i < len && p[i] != '.' ) i++;
	// . hash from p to p[i]
	// . tag names are always ascii, so use the ascii hasher, not utf8
	h = hash64Upper_a ( start , &p[i] - start , h );
	// bail if done
	if ( i >= len ) return h;
	// then period
	h = hash64 ( "." , 1 , h );
	// skip period
	i++;
	// start now points to next word
	start = &p[i];
	// continue
	goto loop;
}

// . return -1 if not found
// . "tagName" is compound (i.e. "myhouse.myroom" )
long Xml::getNodeNum ( long n0 , long n1 , char *tagName , long tagNameLen ) const {
	// . since i changed the hash to a zobrist hash, hashing
	//   "dns.ip" is not the same as hashing "dns" then "." then "ip"
	//   by passing the hash of the last to the next as the startHash
	// . therefore, i now parse it up
	long long h = getCompoundHash ( tagName , tagNameLen );
	long i;
	if ( n1 > m_numNodes ) n1 = m_numNodes;
	if ( n0 > m_numNodes ) n0 = m_numNodes;
	if ( n1 < 0 ) n1 = 0;
	if ( n0 < 0 ) n0 = 0;
	for ( i = n0 ; i < n1; i++ ) {
		// if node is text (non-tag) then skip
		if ( ! m_nodes[i].isTag() ) continue;
		//if ( m_nodes[i].m_compoundHash == h ) break;
		if ( m_nodes[i].m_hash == h ) break;
	}
	// return -1 if not found at all
	if ( i >= n1 ) return -1;
	return i;
}

void Xml::reset ( ) {
	// free old nodes array if any
	if ( m_nodes ) mfree ( m_nodes, m_maxNumNodes*sizeof(XmlNode),"Xml1"); 
	if ( m_ownData && m_xml ) mfree ( m_xml, m_allocSize, "Xml1");
	m_xml         = NULL;
	m_nodes       = NULL; 
	m_numNodes    = 0;
	m_maxNumNodes = 0;
	m_allocSize   = 0;
}

#include "HttpMime.h" // CT_JSON

// "s" must be in utf8
bool Xml::set ( char  *s             , 
	        long   slen          , 
	        bool   ownData       , 
	        long   allocSize     ,
	        bool   pureXml       ,
	        long   version       ,
	        bool   setParentsArg ,
	        long   niceness      ,
		char   contentType ) {

	// just in case
	reset();
	m_niceness = niceness;
	// clear it
	g_errno = 0;

	// if we own the data we free on reset/destruction
	m_ownData = ownData;
	m_version = version;
	// use explicit allocSize if we passed one
	m_allocSize = allocSize?allocSize:slen+1;
	// make pointers to data
	m_xml    = s;
	m_xmlLen = slen; //i;
	// debug msg time
	if ( g_conf.m_logTimingBuild )
		logf(LOG_TIMING,
		    "build: xml: set: 4a. %llu",gettimeofdayInMilliseconds());
	// sanity check
	if ( !s || slen <= 0) return true;
	if ( s[slen] != '\0' ) {
		log(LOG_LOGIC,"build: Xml: Content is not null terminated.");
		char *xx = NULL; *xx = 0;
		//sleep(100);
		g_errno = EBADENGINEER;
		return false;
	}

	// if json go no further. TODO: also do this for CT_TEXT etc.
	if ( contentType == CT_JSON ) {
		m_numNodes = 0;
		// make the array
		m_maxNumNodes = 1;
		m_nodes =(XmlNode *)mmalloc(sizeof(XmlNode)*m_maxNumNodes,"x");
		if ( ! m_nodes ) return false;
		XmlNode *xd = &m_nodes[m_numNodes];
		// hack the node
		xd->m_node       = s;
		xd->m_nodeLen    = slen;
		xd->m_isSelfLink = 0;
		// . nodeId for text nodes is 0
		xd->m_nodeId     = 0;
		xd->m_hasBackTag = false;
		xd->m_hash       = 0;
		xd->m_pairTagNum = -1;
		m_numNodes++;
		return true;
	}


	QUICKPOLL((niceness));
	long i;

	// . replacing NULL bytes with spaces in the buffer
	// . utf8 should never have any 0 bytes in it either!
	for ( i = 0 ; i < slen ; i++ ) if ( !s[i] ) s[i] = ' ';

	// counting the max num nodes
	for ( i = 0 ; s[i] ; i++ ) if ( s[i] == '<' ) m_maxNumNodes++;

	// account for the text (non-tag) nodes (padding nodes between tags)
	m_maxNumNodes *= 2 ;
	// if we only have one tag we can still have 3 nodes!
	m_maxNumNodes++;

	// debug msg time
	if ( g_conf.m_logTimingBuild )
		logf(LOG_TIMING,
		    "build: xml: set: 4b. %llu",gettimeofdayInMilliseconds());

	// . truncate it to avoid spammers
	// . now i limit to 30k nodes because of those damned xls docs!
	// . they have 300,000+ nodes some of 'em

	// now allow 35k nodes for every 100k doclen
	long num100k = slen/(100*1024);
	if (num100k <= 0) num100k = 1;
	long bigMax = 35*1024 * num100k;
	if (m_maxNumNodes > bigMax){
		log(LOG_WARN, "build: xml: doclen %ld, "
		    "too many nodes: counted %ld, max %ld "
		    "...truncating", slen, m_maxNumNodes, bigMax);
		m_maxNumNodes = bigMax;
	}

	// breathe
	QUICKPOLL ( niceness );

	m_nodes = (XmlNode *) mmalloc (sizeof(XmlNode) * m_maxNumNodes,"Xml1");
	if ( ! m_nodes ) { 
		reset(); 
		return log("build: Could not allocate %li "
			   "bytes need to parse document.",
			   sizeof(XmlNode)*m_maxNumNodes);
	}

	// debug msg time
	if ( g_conf.m_logTimingBuild )
		logf(LOG_TIMING,
		    "build: xml: set: 4c. %llu",gettimeofdayInMilliseconds());

	// . TODO: do this on demand
	// . now fill our nodes array
	// . loop over the xml
	// . i is byte-index in buffer
	long oldi;
	for ( i = 0 ; i < m_xmlLen && m_numNodes < m_maxNumNodes ; ) {
		// breathe
		QUICKPOLL(niceness);
		// remember oldi
		oldi = i;
		// set that node
		i += m_nodes[m_numNodes].set (&m_xml[i],pureXml,version);
		// in script?
		if ( m_nodes[m_numNodes].m_nodeId != TAG_SCRIPT ) {
			m_numNodes++;
			continue;
		}
		if ( ! m_nodes[m_numNodes].isFrontTag() ) {
			m_numNodes++;
			continue;
		}
		// ok, we got a <script> tag now
		m_numNodes++;
		// scan for </script>
		char *pstart = &m_xml[i];
		char *p      = pstart;
		char *pend   = &m_xml[0] + m_xmlLen;
		// scan -- 5 continues -- node 1570 is text of script
		for ( ; p < pend ; p++ ) {
			if (            p[0]  != '<' ) continue;
			if ( to_lower_a(p[1]) != '/' ) continue;
			if ( to_lower_a(p[2]) != 's' ) continue;
			if ( to_lower_a(p[3]) != 'c' ) continue;
			if ( to_lower_a(p[4]) != 'r' ) continue;
			if ( to_lower_a(p[5]) != 'i' ) continue;
			if ( to_lower_a(p[6]) != 'p' ) continue;
			if ( to_lower_a(p[7]) != 't' ) continue;
			break;
		}
		// make sure we do not breach! i saw this happen once!
		if ( m_numNodes >= m_maxNumNodes ) break;
		XmlNode *xn      = &m_nodes[m_numNodes++];
		xn->m_nodeId     = TAG_SCRIPTTEXT;//0; // TEXT NODE
		xn->m_node       =     pstart;
		xn->m_nodeLen    = p - pstart;
		xn->m_tagName    = NULL;
		xn->m_tagNameLen = 0;
		xn->m_hasBackTag = false;
		xn->m_hash       = 0;
		xn->m_isVisible  = false;
		xn->m_isBreaking = false;
		// advance i to get to the </script>
		i = p - &m_xml[0] ;
	}
	// sanity
	if ( m_numNodes > m_maxNumNodes ) { char *xx=NULL;*xx=0; }
	// trim off last node if empty! it is causing a core in isBackTag()
	if ( m_numNodes > 0 && m_nodes[m_numNodes-1].m_nodeLen == 0 )
		m_numNodes--;
	// debug msg time
	if ( g_conf.m_logTimingBuild )
		logf(LOG_TIMING,
		    "build: xml: set: 4d. %llu",gettimeofdayInMilliseconds());

	return true;
}

// for translating HTML entities to an iso char
#include "Entities.h"

// . replaces line-breaking html tags with 2 returns if "includeTags" is false
// . stores tags too if "includeTags" is true
// . returns # chars written to buf
// . NOTE: see XmlNode.cpp for list of tag types in "NodeType" structure
// . used to get xml subtrees as text
// . used to get <TITLE>'s
// . must write to your buf rather than just return a pointer since we may
//   have to concatenate several nodes together, we may have to replace tags,..
// . TODO: nuke this in favor of Pos.cpp::filter() -- but that needs Words.cpp
long Xml::getText ( char  *buf             ,
		    long   bufMaxSize      ,
		    long   node1           ,
		    long   node2           ,
		    bool   includeTags     ,
		    bool   visibleTextOnly ,
		    bool   filter          , // convert entities, \r's
		    bool   filterSpaces    , // filter excessive punct/spaces
		    bool   useStopIndexTag ) { // indexable text only?

	// init some vars
	long i    = node1;
	long n    = node2;
	// truncate n to the # of nodes we have
	if ( n > m_numNodes ) n = m_numNodes;
	// keep a non visible tag stack
	long notVisible = 0;
	// are we in indexable area?
	bool inStopTag = false;

	// the destination
	char *dst    = buf;
	char *dstEnd = buf + bufMaxSize;

	char cs = -1;

	// cannot allow nested script tags, messed up our summary generator
	// when a page tried to print a <SCRIPT> tag in a doWrite('<SCRIPT>')
	// already in a <SCRIPT> block
	//char inScript = false;

	// loop through all nodes from here on until we run outta nodes...
	// or until we hit a tag with the same depth as us.
	for ( ; i < n ; i++ ) {

		// if it's <stop index> continue until start index
		if ( useStopIndexTag ) {
			// is it a <stop index> tag ?
			if ( m_nodes[i].m_nodeId == 115 ) {
				inStopTag = true;
				continue;
			}
			// is it a start tag?
			if ( m_nodes[i].m_nodeId == 114 ) {
				inStopTag = false;
				continue;
			}
			// continue if in a stop section
			if ( inStopTag ) continue;
		}
		
		// . set skipText to true if this tag has inivisble text
		// . examples: <option> <script> ...
		if ( m_nodes[i].isTag() && ! m_nodes[i].isVisible() &&
		     m_nodes[i].hasBackTag() ) {
			if ( m_nodes[i].isFrontTag() ) notVisible++;
			else                           notVisible--;
			if ( notVisible < 0 ) notVisible = 0;
		}

		// . if it's a tag then write a \n\n or \n to the buf
		// . do this only if we do not include tags
		// . do it only if there's something already in the buf
		if ( ! includeTags && m_nodes[i].isTag() ) {
			// do nothing if buf still empty
			if ( dst <= buf ) continue;
			// or not a breaking tag
			if ( ! m_nodes[i].isBreaking() ) continue;
			// forgot this check! leave room for terminating \0
			if ( dst + 3 >= dstEnd ) break;
			// if we're not junk filtering just add 2 \n's
			if ( ! filterSpaces ) {
				*dst++='\n';
				*dst++='\n';
				continue;
			}

			// need at least 2 chars in the dst buf so far
			if ( dst - 1 <= buf           ) continue;
			if ( cs == -1                 ) continue;
			// . if prev char is punct, do nothing.
			// . check prev prev char to make sure not a single chr
			// . TODO: fix this!
			if ( is_punct_a( *(dst - cs))) continue;
			//if ( is_punct_utf8( dst[-1])  ) continue;
			if ( i+1 >= n                 ) continue;
			if ( is_punct_utf8 ( &m_nodes[i+1].m_node[0] ) 
			     && !m_nodes[i+1].isTag() ) continue;
			// . watch out for punct before space(s) though
			// . it also ensures that this char is the first char
			//   of any potential multi-byte sequence
			if ( is_wspace_utf8 ( dst - cs ) ) {
				// back up one before that even
				char *f = dst - cs - 1;
				// don't do a while loop on this 
				// cuz with those xls docs we can 
				// have a TON of spaces cuz their 
				// just a bunch of <td></td>&nbsp;'s
				if ( f > buf && is_wspace_a ( *f ) ) f--;
				if ( f > buf && is_wspace_a ( *f ) ) f--;
				if ( f > buf && is_wspace_a ( *f ) ) f--;
				if ( f > buf && is_ascii(*f)&&is_punct_a(*f) )
					continue;
			}
			// ok, add the ".."
			*dst++='.';
			*dst++='.';
			continue;
		}

		// if this tag/text is not visible then continue
		if ( notVisible ) continue; 

		// . get a ptr to the node's data
		// . is 1 of 3 things: a text blob, xml tag or html tag
		char *nodeData    = m_nodes[i].getNode   ();
		int   nodeDataLen = m_nodes[i].getNodeLen();

		// . truncate the node if it's too big
		// . make sure we truncate at a non alphanumeric character
		// . avoid breaking in the middle of a word
		// . we cannot truncate tags
		if ( dst + nodeDataLen  >= dstEnd ) { // bufMaxSize ) {
			// cannot truncate tags
			if ( m_nodes[i].isTag() ) break;
			nodeDataLen = dstEnd - dst - 2;//bufMaxSize - blen;
			while ( nodeDataLen > 0  && 
				! is_wspace_a(nodeData[ nodeDataLen-1 ])) 
				nodeDataLen--;
		}
		
		// if we truncated the whole thing just break out, we're done.
		if ( nodeDataLen <= 0 ) break;

		// . copy the node data into our buffer
		// . translate HTML entities to iso characters
		// . translate \r's into spaces

		// point to it
		char *src    = nodeData;
		char *srcEnd = nodeData + nodeDataLen;
		// size of character in bytes, usually 1
		//char cs ;
		// copy the node @src into "dst"
		for ( ; src < srcEnd ; src += cs , dst += cs ) {
			// get the character size in bytes
			cs = getUtf8CharSize ( src );
			// no back to back spaces if we're filtering junk
			if ( filterSpaces && is_wspace_utf8 ( src ) ) {
				if ( dst     <= buf ) {dst -= cs; continue;}
				if ( dst[-1] == ' ' ) {dst -= cs; continue;}
				// ok, do not filter it
				//goto simplecopy;
			}

			// a lot of docs have ^M's in them (\r)
			//if ( c == '\r' ) { buf [ blen++ ] = ' '; continue; }

			// store it as-is if not filtering or not html entity
			//simplecopy:
			// if more than 1 byte in char, use memcpy
			if ( cs > 1 ) memcpy ( dst , src , cs );
			else          *dst = *src;
		}
		// continue looping over nodes (text and tag nodes)
	}

	// . strip trailing spaces
	// . is_wspace_utf8 will be false if it is not the first character
	//   of a utf8 char sequence, and i don't count any multi-byte
	//   spaces i guess...
	while ( dst > buf && is_wspace_a ( dst[-1] ) ) dst--;

	// null term it
	*dst = '\0';

	// return the # of bytes we've written into the buffer.
	return dst - buf;
}

// just get a pointer to it
char *Xml::getMetaContentPointer ( char *field    ,
				   long  fieldLen ,
				   char *name     ,
				   long *slen     ) {
	// find the first meta summary node
	for ( long i = 0 ; i < m_numNodes ; i++ ) {
		// continue if not a meta tag
		if ( m_nodes[i].m_nodeId != 68 ) continue;
		// . does it have a type field that's "summary"
		// . <meta name=summary content="...">
		// . <meta http-equiv="refresh" content="0;URL=http://y.com/">
		long len;
		char *s = getString ( i , name , &len );
		// continue if name doesn't match field
		if ( len != fieldLen ) continue;
		// field can be "summary","description","keywords",...
		if ( strncasecmp ( s , field , fieldLen ) != 0 ) continue;
		// point to the summary itself
		s = getString ( i , "content" , &len );
		if ( ! s || len <= 0 ) continue;
		// return the pointer (and set the length of what it points to)
		*slen = len;
		return s;
	}
	*slen = 0;
	return NULL;
}

// . extract the content from a meta tag
// . null terminate it and store it into "buf"
// . field can be stuff like "summary","description","keywords",...
// . TODO: have a filter option to filter out back-to-back spaces for summary
//         generation purposes in Summary class
// . "name" is usually "name" or "http-equiv"
// . if "convertHtmlEntities" is true we turn < into &lt; and > in &gt;

long Xml::getMetaContent (char *buf, long bufLen, char *field, long fieldLen ,
			  char *name , bool convertHtmlEntities , 
			  long startNode , long *matchedNode ) {
	// return 0 length if no buffer space
	if ( bufLen <= 0 ) return 0;
	// assume it's empty
	buf[0] = '\0';
	// assume no tag matched
	if ( matchedNode ) *matchedNode = -1;
	// store output into "dst"
	char *dst    = buf;
	char *dstEnd = buf + bufLen;
	// find the first meta summary node
	for ( long i = startNode ; i < m_numNodes ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// continue if not a meta tag
		if ( m_nodes[i].m_nodeId != 68 ) continue;
		// . does it have a type field that's "summary"
		// . <meta name=summary content="...">
		// . <meta http-equiv="refresh" content="0;URL=http://y.com/">
		long len;
		char *s = getString ( i , name , &len );
		// continue if name doesn't match field
		// field can be "summary","description","keywords",...
		if ( len != fieldLen ) continue;
		if ( strncasecmp ( s , field , fieldLen ) != 0 ) continue;
		// point to the summary itself
		s = getString ( i , "content" , &len );
		if ( ! s || len <= 0 ) continue;
		// point to it
		char *src    = s;
		char *srcEnd = s + len;
		// size of character in bytes, usually 1
		char cs ;
		// bookmark
		char *lastp = NULL;
		// copy the node @p into "dst"
		for ( ; src < srcEnd ; src+= cs ) {
			// breathe
			QUICKPOLL(m_niceness);
			// get the character size in bytes
			cs = getUtf8CharSize ( src );
			// break if we are full! (save room for \0)
			if ( dst + 5 >= dstEnd ) break;
			// remember last punct for cutting purposes
			if ( ! is_alnum_utf8 ( src ) ) lastp = dst;
			// encode it as an html entity if asked to
			if ( *src == '<' && convertHtmlEntities ) {
				memcpy ( dst , "&lt;" , 4 );
				dst += 4;
				continue;
			}
			// encode it as an html entity if asked to
			if ( *src == '>' && convertHtmlEntities ) {
				memcpy ( dst , "&gt;" , 4 );
				dst += 4;
				continue;
			}
			// if more than 1 byte in char, use memcpy
			if ( cs > 1 ) memcpy ( dst , src , cs );
			else          *dst = *src;
			dst += cs;
		}

		// continue looping over nodes (text and tag nodes)

		// do not split a word in the middle! so if we had to
		// truncate, at least try to truncate at last punctuation
		// mark if we had one.
		if ( dst + 5 >= dstEnd && lastp ) {
			*lastp = '\0';
			len = lastp - buf;
		}
		// end at dst as well
		else {
			*dst = '\0';
			len = dst - buf;
		}

		// store node number
		if ( matchedNode ) *matchedNode = i;
		return len;
	}
	return 0;
}

bool Xml::hasGigablastForm(char **url, long *urlLen) {
	// find the first meta summary node
	for ( long i = 0 ; i < m_numNodes ; i++ ) {
		// continue if not a FORM tag
		if ( m_nodes[i].m_nodeId != 40 ) continue;
		// <form method=get action=/cgi/0.cgi name=f>
		long len;
		char *s = getString ( i , "action" , &len );
		if (url) *url = s;
		if (urlLen) *urlLen = len;
		// skip http://
		if ( len > 10 && strncasecmp(s,"http://",7)==0 ) {
			s += 7; len -= 7; }
		// skip https://
		if ( len > 11 && strncasecmp(s,"https://",8)==0 ) {
			s += 8; len -= 8; }
		// skip www.
		if ( len > 4 && strncasecmp(s,"www.",4)==0) {
			s += 4; len -= 4; }
		// skip sitesearch.
		if ( len > 11 && strncasecmp(s,"sitesearch.",11)==0) {
			s += 11; len -= 11; }
		// need gigablast.com
		if ( len < 13 || strncasecmp(s,"gigablast.com",13)!=0)
			continue;
		// skip gigablast.com
		s += 13; len -= 13;
		// need slash
		if ( len < 2 || *s != '/' ) continue;
		// skip slash
		s += 1; len -= 1;

		// action must be http://www.gigablast.com/cgi/0.cgi EXACTLY
		if ( len == 9 && strncasecmp(s,"cgi/0.cgi",9)==0) return true;
		// another forms
		if ( len == 6 && strncasecmp(s,"search"   ,6)==0) return true;
		// another form
		if ( len == 9 && strncasecmp(s,"index.php",9)==0) return true;
	}
	return false;
}

//  TEST CASES:
//. this is NOT rss, but has an rdf:rdf tag in it!
//  http://www.silverstripe.com/silverstripe-adds-a-touch-of-design-and-a-whole-lot-more/
//  http://government.zdnet.com/?p=4245
long Xml::isRSSFeed ( ) {
	// must have atleast one rss.channel.item.link node
	//long rssLink = getNodeNum ( "rss.channel.item.link" );
	//if ( rssLink >= 0 )
	//	return true;
	// rdf: must have atleast one rss.channel.item.link node
	//rssLink = getNodeNum ( "rdf.channel.item.link" );
	//if ( rssLink >= 0 )
	//	return true;
	//bool hasTag  = false;
	long type = 0;
	long tag  = 0;
	long i;
	for ( i = 0; i < m_numNodes; i++ ) {
		// skip text nodes (nodeId is 0)
		if ( m_nodes[i].m_nodeId == TAG_TEXTNODE ) continue;
		// check for RSS/FEED/RDF node
		if ( m_nodes[i].m_nodeId == TAG_RDF  ) { 
			tag = TAG_RDF; type = 1; }
		if ( m_nodes[i].m_nodeId == TAG_RSS  ) {
			tag = TAG_RSS; type = 1; }
		if ( m_nodes[i].m_nodeId == TAG_FEED ) {
			tag = TAG_FEED; type = 6; }
		if ( tag ) break;
	}
	// if no such tag we are definitely not rss
	if ( ! tag ) return 0;
	// i have only seen rdf tags embedded in html
	if ( tag != TAG_RDF ) return type;
	// . now check for a <channel>, <item> or <link> tag
	// . we need one of those to be useful
	for ( i = 0; i < m_numNodes; i++ ) {
		if ( m_nodes[i].m_nodeId == TAG_CHANNEL ) return type;
		if ( m_nodes[i].m_nodeId == TAG_ITEM    ) return type;
		if ( m_nodes[i].m_nodeId == TAG_ENTRY   ) return type;
		//if ( m_nodes[i].m_nodeId == TAG_LINK    ) return type;
	}
	return 0;
}

char *Xml::getRSSTitle ( long *titleLen , bool *isHtmlEncoded ) const {
	// assume it is html encoded (i.e. <'s are encoded as &lt;'s)
	*isHtmlEncoded = true;
	// . extract the RSS/Atom title
	// rss/rdf
	long tLen;
	//char *title = getString ( "item.title",
	//			  &tLen       ,
	//			  true        );
	char *title = getString ( "title" ,
				  &tLen   ,
				  true    );
	// atom
	//if ( ! title )
	//	title = getString ( "entry.title",
	//			    &tLen        ,
	//			    true         );
	// watch out for <![CDATA[]]> block
	if ( tLen >= 12 && strncasecmp(title, "<![CDATA[", 9) == 0 ) {
		title += 9;
		tLen  -= 12;
		*isHtmlEncoded = false;
	}
	// return
	*titleLen  = tLen;
	return title;
}

char *Xml::getRSSDescription ( long *descLen , bool *isHtmlEncoded ) {
	// assume it is html encoded (i.e. <'s are encoded as &lt;'s)
	*isHtmlEncoded = true;
	// . extract the RSS/Atom description
	// rss/rdf
	long dLen;
	char *desc  = getString ( "description", // "item.description",
				  &dLen        ,
				  true         );
	// get content first, it is usually more inclusive than the summary
	if ( ! desc )
		desc  = getString ( "content" , // "entry.content",
				    &dLen     ,
				    true      );
	// atom
	if ( ! desc )
		desc  = getString ( "summary" , // "entry.summary",
				    &dLen     ,
				    true      );
	// watch out for <![CDATA[]]> block
	if ( dLen >= 12 && strncasecmp(desc, "<![CDATA[", 9) == 0 ) {
		desc += 9;
		dLen -= 12;
		*isHtmlEncoded = false;
	}
	// return
	*descLen = dLen;
	return desc;
}

// get a link to an RSS feed for this page
char *Xml::getRSSPointer ( long *length, long  startNode, long *matchedNode ) {
	// assume no tag matched
	if ( matchedNode ) *matchedNode = -1;
	*length = 0;
	// find the first meta summary node
	for ( long i = startNode ; i < m_numNodes ; i++ ) {
		// continue if not a <link> tag
		if ( m_nodes[i].m_nodeId != TAG_LINK ) continue;
		// . check for rel="alternate"
		long len;
		char *s = getString ( i, "rel", &len );
		// continue if name doesn't match field
		// field can be "summary","description","keywords",...
		if ( len != 9 ) continue;
		if ( strncasecmp ( s , "alternate" , 9 ) != 0 ) 
			continue;
		// . check for valid type:
		//   type="application/atom+xml" (atom)
		//   type="application/rss+xml"  (RSS 1.0/2.0)
		//   type="application/rdf+xml"  (RDF)
		//   type="text/xml"             (RSS .92) support?
		s = getString ( i, "type", &len );
		if (len == 20 && !strncasecmp(s, "application/atom+xml", 20))
			goto isRSS;
		if (len == 19 && !strncasecmp(s, "application/rss+xml", 19) )
			goto isRSS;
		if (len == 19 && !strncasecmp(s, "application/rdf+xml", 19) )
			goto isRSS;
		// not RSS
		continue;
	isRSS:
		// . extract the link from href=""
		s = getString ( i, "href", length );
		if ( matchedNode ) *matchedNode = i;
		return s;
	}
	return NULL;
}

// get the link pointed to by this RSS item
char *Xml::getItemLink ( long *linkLen ) {
	char *link = NULL;
	*linkLen = 0;

	// find the first meta summary node
	for ( long i = 0; i < m_numNodes ; i++ ) {

		// skip node if not an xml tag node
		//if ( m_nodes[i].m_nodeId != 1 ) continue;

		if ( m_nodes[i].m_nodeId == TAG_ENCLOSURE ) {
			link = (char *) getString ( i, "url", linkLen );
			if ( link ) return link;
		}
		// skip if not a <link> tag
		else if ( m_nodes[i].m_nodeId != TAG_LINK ) 
			continue;

		// do we have an "enclosure" tag? used for multimedia.
		/*
		if ( m_nodes[i].m_tagNameLen == 9 &&
		     memcmp(m_nodes[i].m_tagName,"enclosure",9) == 0 ) 
			link = (char *) getString ( i, "url", linkLen );

		if ( link ) return link;

		// skip if not a <link> tag
		if ( m_nodes[i].m_tagNameLen != 4 ) continue;
		if (memcmp(m_nodes[i].m_tagName,"link",4)!=0) continue;
		}
		*/

		// check for href string in the <link> tag... wierd...
		link = getString ( i, "href", linkLen );
		if ( link ) return link;

		// if not in href, get the following text node
		char *node    = m_nodes[i].m_node;
		long  nodeLen = m_nodes[i].m_nodeLen;
		// must not end in "/>"
		if (node[nodeLen-2] == '/' ) continue;
		// expect <link> url </link>
		if ( i + 2 >= m_numNodes ) continue;
		if ( !isBackTag(i+2) ) continue;
		// get the url
		link     = m_nodes[i+1].m_node;
		*linkLen = m_nodes[i+1].m_nodeLen;

		if ( link && &linkLen > 0 ) return link;
	}
	// no link found, return NULL
	*linkLen = 0;
	return NULL;
}


long Xml::findNodeNum(char *nodeText) {
        // do a binary search to find the node whose text begins at 
        // this pointer
	long a = 0;
	long b = m_numNodes - 1;
	if ( nodeText < m_nodes[a].m_node ) return -1;
	if ( nodeText > m_nodes[b].m_node ) return -1;
	
	while (a < b) {
		long mid = ((b-a)>>1) + a;
		if ( nodeText == m_nodes[a].m_node ) return a;
		if ( nodeText == m_nodes[b].m_node ) return b;
		if ( nodeText == m_nodes[mid].m_node ) return mid;
		
		if ( nodeText < m_nodes[mid].m_node ) b = mid - 1;
		else a = mid + 1;
	}

	return -1;
}

// returns -1 if no count found
long Xml::getPingServerCount ( ) {
	for ( long i = 0 ; i < m_numNodes && i < 40 ; i++ ) {
		if ( ! m_nodes[i].isXmlTag() ) continue;
		// must be "weblogUpdates"
		long  slen = 0;
		char *s = getString ( i,"count",&slen);
		if ( ! s || slen <= 0 ) continue;
		long count = atoi(s);
		if ( count == -1 ) continue;
		// got it
		return count;
	}
	// no count
	return -1;
}
