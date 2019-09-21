#include "gb-include.h"

#include <sys/stat.h>
#include "Titledb.h"
#include "Tagdb.h"
#include "Categories.h"
#include "Unicode.h"
#include "Threads.h"
#include "Msg1.h"
#include "HttpServer.h"
#include "Pages.h"
#include "SiteGetter.h"
#include "HashTableX.h"
#include "Users.h"
#include "Process.h"
#include "Rebalance.h"

static void gotMsg0ReplyWrapper ( void *state );
//static void gotReplyWrapper9a   ( void *state , UdpSlot *slot ) ;

//static void gotList ( void *state , RdbList *xxx , Msg5 *yyy ) ;
//static void sendReply9a ( void *state ) ;

static HashTableX s_ht;

static bool s_initialized = false;

// to stdout
int32_t Tag::print ( ) {
	SafeBuf sb;
	printToBuf ( &sb );
	// dump that
	return fprintf(stderr,"%s\n",sb.getBufStart());
}

bool Tag::printToBuf ( SafeBuf *sb ) {

	sb->safePrintf("k.hsthash=%016"XINT64" "
		       "k.duphash=%08"XINT32" "
		       "k.sitehash=%08"XINT32" ",
		       m_key.n1,
		       (int32_t)(m_key.n0>>32),
		       (int32_t)(m_key.n0&0xffffffff));
	// print the tagname
	sb->safePrintf ( "TAG=%s,\"%s\",", 
			 getTagStrFromType(m_type),
			 getUser() );
	// data size
	//sb->safePrintf( "%"INT32",", (int32_t)getTagDataSize());
	// print the date when this tag was added
	time_t ts = m_timestamp;
	struct tm *timeStruct = localtime ( &ts );
	char tmp[100];
	strftime(tmp,100,"%b-%d-%Y-%H:%M:%S,",timeStruct);
	sb->safePrintf("%s(%"UINT32"),",tmp,m_timestamp);
	// print the time as a int32_t, seconds since epoch
	//sb->safePrintf("%"UINT32",",m_timestamp);
	// print the ip added from
	sb->safePrintf("%s,",iptoa(m_ip));
	// print the tag id
	//sb->safePrintf("%"UINT32",\"",(int32_t)m_tagId);
	// key.n1 is hash of the subdomain i think
	//sb->safePrintf("%"UINT32",\"",m_key.n1);
	sb->safePrintf("\"");
	if ( ! printDataToBuf ( sb ) ) return false;
	// final quote
	sb->safePrintf("\"");
	return true;
}

// . "site" can also be a specific url, but it must be normalized
// . i.e. of the form http://xyz.com/
void Tag::set ( char *site ,
		char *tagname ,
		int32_t  timestamp ,
		char *user ,
		int32_t  ip ,
		char *data ,
		int32_t  dataSize ) {
	// get type from name
	m_type = getTagTypeFromStr ( tagname , strlen(tagname) );
	// sanity
	//isTagTypeIndexable ( m_type );
	m_timestamp = timestamp;
	m_ip        = ip;
	int32_t userLen = 0;
	if ( user ) userLen = gbstrlen(user);
	// truncate to 127 byte int32_t
	if ( userLen > 126 ) userLen = 126;
	// first byte is size of user, then user plus \0 then data
	//m_bufSize  = 1 + userLen + 1 + dataSize;
	// "site" must skip http://
	//int32_t slen = gbstrlen(site);
	//if      ( slen > 8 && strncasecmp(site,"http://",7)==0 ) 
	//	site += 7;
	//else if ( slen > 8 && strncasecmp(site,"https://",8)==0 ) 
	//	site += 8;

	// normalize
	Url norm;
	norm.set ( site );

	// store user into special buffer
	//int32_t ulen = 0;
	//if ( user ) {
	//	ulen = gbstrlen(user);
	//	if ( ulen > 7 ) ulen = 7;
	//}
	//memset ( m_user , 0    , 8    );
	//gbmemcpy ( m_user , user , ulen );
	char *p = m_buf;
	// store size (includes \0)
	*p++ = userLen + 1;
	// then user name
	gbmemcpy ( p , user , userLen );
	p += userLen;
	// then \0
	*p++ = '\0';
	// store data now too
	gbmemcpy ( p , data , dataSize );
	p += dataSize;
	// NULL terminate if they did not! now all tag are strings and must
	// be NULL terminated.
	if ( data && p[-1] ) { // data && m_data[dataSize-1] ) {
		//m_data[dataSize] = '\0';
		*p++ = '\0';
		//dataSize++;
		//m_dataSize++;
	}
	// set it
	m_bufSize = p - m_buf;

	// top X bits should be hash of the domain only so all recs are on the
	// same host near each other
	//m_key.n1 = hash32 ( norm.getDomain() , norm.getDomainLen());
	//
	// too many tags were being read when k.n1 was the domain hash for
	// sites like az.com that had hundreds of subdomains. so go based on
	// host instead.
	//
	// CRAP: using 32 bit hash we get collisions for crap like
	// thedietsolutionprogramscam.com and
	// 2witchdoctors.a-livejasmin.com
	// so let's move to 64bit keys
	//m_key.n1 = hash64 ( norm.getHost() , norm.getHostLen());
	// i had to make this the hash of the site, not host, 
	// because www.last.fm/user/xxxxx/
	// was making the rdblist a few megabytes big!!
	m_key.n1 = hash64n ( site );
	// assume we are unique tag, that many of this type can exist
	uint32_t upper32 = getDedupHash(); // m_type;
	/*
	// if we are NOT unique... then hash username and data. thus we only
	// replace a key if its the same tagtype, username and data. that
	// way it will just update the timestamp and/or ip.
	if ( ! isTagTypeUnique ( m_type ) ) {
		// start hashing here
		char *startHashing = (char *)&m_type;
		// end here. include username (and tag data!)
		char *endHashing = m_buf + m_bufSize;
		// hash this many bytes
		int32_t hashSize = endHashing - startHashing;
		// . set key
		upper32 = hash32 ( startHashing , hashSize );
	}
	*/

	// put in upper 32
	m_key.n0 = upper32;
	// shift it up
	m_key.n0 <<= 32;
	// . then or in url hash
	// . for the site "www.paypal.com:1234" this included the port!
	//   but for the most part if the site is just a hostname then
	//   this is basically just a hostname, too, but the hash will
	//   include the http:// and the ending /
	// . www.paypal.com:1234 was added as a site. so it has the
	//   same m_key.n1 as www.paypal.com, but this part is different
	//   here. this is the full site hash really. so during the lookup
	//   i'd say filter out such tags if they don't match the site you
	//   are looking up.
	//m_key.n0 |= (uint32_t) hash32 ( norm.getUrl() , norm.getUrlLen() );
	// set positive bit so its not a delete record
	m_key.n0 |= 0x01;

	// the size of this class as an Rdb record
	m_recDataSize = m_bufSize + sizeof(Tag) - sizeof(key128_t) - 4;
}

// . return # of ascii chars scanned in "p"
// . return 0 on error
// . parses output of printToBuf() above
// . k.n1=0x695b3 k.n0=0xa4118684fa4edf93 version=0 TAG=ruleset,"mwells",Jan-02-2009-18:26:04,<timestamp>,67.16.94.2,3735437892,36 TAG=blog,"mwells",Jan-02-2009-18:26:04,67.16.94.2,2207516434,1 TAG=site,"tagdb",Jan-02-2009-18:26:04,0.0.0.0,833534375,mini-j-gaidin.livejournal.com/
int32_t Tag::setFromBuf ( char *p , char *pend ) {
	// save our place
	char *start = p;
	// tags always start with " TAG="
	if ( strncmp(p," TAG=",5) ) {
		log("tagdb: error processing tag in setFromBuf().");
		return 0;
	}
	// skip that
	p += 5;

	// get the type
	char *type = p;
	// get type length
	while ( p < pend && *p != ',' ) p++;
	// error?
	if ( p == pend ) return 0;
	// that is the length
	int32_t typeLen = p - type;
	// convert to number
	m_type = getTagTypeFromStr ( type , typeLen );
	// panic?
	if ( m_type == -1 ) { char *xx=NULL;*xx=0;}
	// now the user, skip comma and quote
	p+=2;

	// data buffer
	char *dst = m_buf;
	// point to it
	char *user = p;
	// get end of it
	while ( p < pend && *p != '\"' ) p++;
	// error?
	if ( p == pend ) return 0;
	// set length
	int32_t userLen = p - user;
	// sanity. username total buf space including \0 <= 8
	if ( userLen > 126 ) userLen = 126;
	// copy it over into us
	//gbmemcpy ( m_user , user , userLen );
	// NULL terminate
	//m_user[userLen] = '\0';
	// first byte is username size
	*dst++ = userLen+1;
	// then the username
	gbmemcpy ( dst , user , userLen );
	dst += userLen;
	// and finall null termination
	*dst++ = '\0';
	// skip quote and comma
	p+=2;

	// now the datasize
	//int32_t m_dataSize = atoi(p);
	// skip till comma
	//while ( p < pend && *p != ',' ) p++;
	// error?
	//if ( p == pend ) return 0;
	// skip comma
	//p++;

	// that is the time stamp in canonical form
	// skip till comma
	while ( p < pend && *p != ',' ) p++;
	// error?
	if ( p == pend ) return 0;
	// skip comma
	p++;

	// save start
	char *ts = p;
	// skip until comma again
	while ( p < pend && *p != ',' ) p++;
	// error?
	if ( p == pend ) return 0;
	// this is the timestamp in seconds since epoch
	m_timestamp = atoi(ts);
	// skip comma
	p++;

	// ip address as text
	char *ips = p;
	// skip until comma again
	while ( p < pend && *p != ',' ) p++;
	// error?
	if ( p == pend ) return 0;
	// convert it to binary
	m_ip = atoip ( ips , p - ips );
	// skip comma
	p++;

	// get the tag identifier
	//m_tagId = atol(p);
	//sscanf ( p , "%"UINT32",",&m_tagId);
	//int64_t big = atoll(p);
	//m_tagId = (int32_t)big;
	// skip until comma again
	//while ( p < pend && *p != ',' ) p++;
	// error?
	//if ( p == pend ) return 0;
	// skip comma
	//p++;

	//
	// BEGIN HACK
	//
	// as a hack for now, override this, because before we were not 100%
	// strings as tags, we had single byte values being printed out as
	// strings of 3 bytes
	//char *e = p;
	//while ( e < pend && ! is_wspace_a(*e) ) e++;
	//if ( e > pend ) return 0;
	//m_dataSize = e - p;
	// add in a \0
	//m_dataSize++;
	
	//
	// END HACK
	//

	// . now is the data
	// . return # of chars scanned in "p"
	p += setDataFromBuf ( p , pend );

	// . sanity check
	// . all tags must be NULL terminated now
	if ( m_buf[m_bufSize-1] != '\0' ) {char *xx=NULL; *xx=0; }

	// we reset this since we now require that all tags are NULL terminated
	// strings
	//m_tagId = hash32 ( (char *)this,(int32_t)sizeof(Tag)+m_dataSize , 0 );
	// 0 is not valid
	//if ( m_tagId == 0 ) m_tagId = 1;

	// return how many bytes we read
	return p - start;
}

// . return # of chars scanned in "p"
// . return 0 on error
int32_t Tag::setDataFromBuf ( char *p , char *pend ) {
	// string are special
	//if ( isTagTypeString ( m_type ) ) {
	// skip over username in the buffer to point to where to put tag data
	char *dst = m_buf + *m_buf + 1;
	// stop at space of 
	gbmemcpy(dst,p,pend-p);
	// advance
	dst += (pend-p);
	// update
	m_bufSize = dst - m_buf;
	// should be end delimter
	char c = m_buf[m_bufSize-1];
	// sanity check
	if ( c && ! isspace(c) ) { char *xx=NULL;*xx=0; }
	// strings are always NULL terminated, the datasize should
	// include the NULL termination
	m_buf[m_bufSize-1]='\0';
	// we basically insert the \0, and *p should point to the space
	// right after the string...! so return m_dataSize - 1
	return m_bufSize - 1;
	/*
	}
	// save it to count
	char *start = p;
	// print as decimal if just 1 byte
	if ( m_dataSize == 1 ) {
		int32_t v = atoi(p);
		if ( v > 256 ) { char *xx=NULL;*xx=0; }
		m_data[0] = v;
		// skip till whitespace or end
		while ( p < pend && isdigit(*p) ) p++;
		return p - start;
	}
	// skip 0x
	if ( *p!='0' || *(p+1)!='x' ) { char *xx=NULL;*xx=0; }
	p += 2;
	// convert hexadecimal string into binary
	int32_t bytesStored = hexToBinary ( p , pend , m_data , false );
	// sanity check
	if ( bytesStored != m_dataSize ) { char*xx=NULL;*xx=0;}
	// advance p, each byte is two characters
	p += bytesStored * 2;
	// return # of bytes in "p" we scanned
	return p - start;
	*/
}

int32_t hexToBinary ( char *src , char *srcEnd , char *dst , bool decrement ) {
	// keep tabs on how many bytes we store into "dst"
	char *start = dst;
	// read in hex values
	while ( src < srcEnd ) {
		// get FIRST hex digit
		unsigned char v;
		v = *(unsigned char *)src;
                if      ( v >= 'a' && v <= 'f' ) v = v - 'a' + 10;
		else if ( v >= 'A' && v <= 'F' ) v = v - 'A' + 10;
		else if ( v >= '0' && v <= '9' ) v = v - '0';
		else break;
		// sanity check
		if ( v >= 16 ) { char *xx=NULL;*xx=0;}
		// next character
		src++;
		// store it in the destination
		*dst = v;
		// sanity check, need one more char FOR SURE!
		if ( src >= srcEnd ) { char*xx=NULL;*xx=0;}
		// get the SECOND hex digit of this byte
		v = *(unsigned char *)src;
                if      ( v >= 'a' && v <= 'f' ) v = v - 'a' + 10;
		else if ( v >= 'A' && v <= 'F' ) v = v - 'A' + 10;
		else if ( v >= '0' && v <= '9' ) v = v - '0';
		else break;
		// sanity check
		if ( v >= 16 ) { char *xx=NULL;*xx=0;}
		// next character
		src++;
		// shift last guy up 4 bits
		*dst = *dst << 4;
		// or in the new guy
		*dst |= v;
		// point to next byte now
		if ( decrement ) dst--;
		else             dst++;
	}
	return dst - start;
}


bool Tag::printDataToBuf ( SafeBuf *sb ) {
	// string are special
	//if ( isTagTypeString ( m_type ) ) {

	char *data     = getTagData();
	int32_t  dataSize = getTagDataSize();
	// because of a bug of not appending the \0 and incrementing
	// Tag::m_dataSize when we should have, we must deal with this!
	//sb->safePrintf("%s",m_data);
	for ( int32_t i = 0 ; data[i] && i < dataSize ; i++ )
		sb->safePrintf ( "%c" , data[i] );
	return true;
	/*
	}
	// print as decimal if just 1 byte
	if ( m_dataSize == 1 ) {
		sb->safePrintf("%"INT32"",(int32_t)m_data[0]);
		return true;
	}
	// the "score"
	sb->safePrintf("0x");
	//for ( int32_t i = 0 ; i < m_dataSize ; i++ )
	//	sb->safePrintf ( "%02hhx" , m_data[m_dataSize-i-1] );
	// i guess just print it first byte first now
	for ( int32_t i = 0 ; i < m_dataSize ; i++ )
		sb->safePrintf ( "%02hhx" , m_data[i] );
	*/
	return true;
}

// /admin/tagdb?c=mdw&u=www.mdw123.com&ufu=&username=admin&tagtype0=sitenuminlinks&tagdata0=10&tagtype1=rootlang&tagdata1=&tagtype2=rootlang&tagdata2=&add=Add+Tags
bool Tag::printToBufAsAddRequest ( SafeBuf *sb ) {
	// print the tagname
	char *str = getTagStrFromType ( m_type );
	sb->safePrintf("/admin/tagdb?");
	// print the site
	//sb->safePrintf("u=");
	//sb->urlEncode ( m_url->getUrl() );
	// print key of the tag as 16 byte key in ascii hex notation
	// we don't know the "site" for all tags because "site" is a tag
	// itself. we should take this in lieu of the "u=" url parm
	// which is made to generate the key anyhow.
	//sb->safePrintf("tagkey0=%s",KEYSTR(&m_key,16));
	sb->safePrintf("&tagn0keyb0=%"INT64"",m_key.n0);
	sb->safePrintf("&tagn1keyb0=%"INT64"",m_key.n1);
	// print the user that added this tag
	sb->safePrintf ( "&username=%s" , getUser() );
	// the tag type, like "sitenuminlinks" or "rootlang"
	sb->safePrintf("&tagtype0=%s",str);
	// print the date when this tag was added
	//sb->safePrintf ("&%s.time=%"INT32"", str, m_timestamp );
	// print the tag id
	//sb->safePrintf("&%s.id=%"UINT32"",str,(int32_t)m_tagId);
	// the "score"
	sb->safePrintf("&tagdata0=");//,str);
	// print the m_data
	SafeBuf tmp;
	if ( ! printDataToBuf ( &tmp ) ) return false;
	tmp.nullTerm();
	sb->urlEncode(tmp.getBufStart());
	sb->nullTerm();
	return true;
}

bool Tag::printToBufAsXml ( SafeBuf *sb ) {
	// print the tagname
	char *str = getTagStrFromType ( m_type );
	// print the user that added this tag
	sb->safePrintf ("\t\t<tag>\n\t\t\t<name>%s</name>\n\t\t\t<user>%s",
			str,getUser());
	// print the date when this tag was added
	sb->safePrintf("</user>\n\t\t\t<timestamp>%"INT32"</timestamp>\n",
		       m_timestamp);
	// print the ip added from
	sb->safePrintf("\t\t\t<ip>%s</ip>\n",iptoa(m_ip));
	// print the tag id
	//sb->safePrintf("\t\t\t<id>%"UINT32"</id>\n",(int32_t)m_tagId);
	// the "score"
	sb->safePrintf("\t\t\t<score>");
	// print the m_data
	if ( ! printDataToBuf ( sb ) ) return false;
	sb->safePrintf("</score>\n\t\t</tag>");
	return true;
}

//if ( ! sb->safePrintf("\t\t<eventTagFromTagdb>"
//		      "<![CDATA[") )
bool Tag::printToBufAsXml2 ( SafeBuf *sb ) {
	// print the tagname
	char *str = getTagStrFromType ( m_type );
	// print the user that added this tag
	sb->safePrintf ("\t\t<eventTagdbTag>\n"
			// who added the tag:
			"\t\t\t<addedBy><![CDATA[%s]]></addedBy>\n"
			// when tag was added:
			"\t\t\t<addedTimestamp>%"UINT32"</addedTimestamp>\n"
			// ip added from
			"\t\t\t<addedFromIP><![CDATA[%s]]></addedFromIP>\n"
			// name of the tag:
			"\t\t\t<name><![CDATA[%s]]></name>\n"
			// the tag data
			"\t\t\t<data><![CDATA[",
			getUser(),
			m_timestamp,
			iptoa(m_ip),
			str);
	// print the m_data
	if ( ! printDataToBuf ( sb ) ) return false;
	sb->safePrintf("]]></data>\n"
		       "\t\t</eventTagdbTag>\n");
	return true;
}

bool Tag::printToBufAsHtml ( SafeBuf *sb , char *prefix ) {
	// print the tagname
	char *str = getTagStrFromType ( m_type );
	// print the user that added this tag
	sb->safePrintf ("<tr><td>%s</td><td><b>%s</b>", prefix, str);
	// the "score"
	sb->safePrintf(" value=<b>");
	// print the m_data
	if ( ! printDataToBuf ( sb ) ) return false;
	// print the date when this tag was added
	sb->safePrintf("</b> user=%s time=",getUser());
	time_t ts = m_timestamp;
	struct tm *timeStruct = localtime ( &ts );
	char tmp[100];
	strftime(tmp,100,"%b-%d-%Y-%H:%M:%S",timeStruct);
	sb->safePrintf("%s(%"UINT32")",tmp,m_timestamp);
	// print the ip added from
	sb->safePrintf(" ip=%s",iptoa(m_ip));
	//sb->safePrintf(" id=%"UINT32"",(int32_t)m_tagId);
	sb->safePrintf("</td></tr>\n");
	return true;
}

bool Tag::printToBufAsTagVector ( SafeBuf *sb ) {
	// print the tagname
	char *str = getTagStrFromType ( m_type );
	// print strings data types special
	//if ( isTagTypeString ( m_type ) ) {
	//sb->safePrintf("%s:%s ",str,m_data);
	sb->safePrintf("%s:",str);
	// print the m_data
	if ( ! printDataToBuf ( sb ) ) return false;
	sb->safePrintf(" ");
	return true;
	/*
	}
	// print the user that added this tag
	sb->safePrintf ("%s:", str );
	if ( ! printDataToBuf ( sb ) ) return false;
	sb->safePrintf(" ");
	return true;
	*/
}

bool Tag::isType ( char *t ) {
	int32_t h = hash32n ( t );
	return (m_type == h);
}



TagRec::TagRec ( ) {
	m_numListPtrs = 0;
}

void TagRec::constructor ( ) {
	m_numListPtrs = 0;
	// run a constructor on the lists
	for ( int32_t i = 0 ; i < MAX_TAGDB_REQUESTS ; i++ ) {
		m_lists[i].constructor();//m_alloc     = NULL;
		//m_lists[i].m_allocSize = 0;
	}
}

TagRec::~TagRec ( ) {
	reset();
}

void TagRec::reset ( ) {
	m_numListPtrs = 0;
	for ( int32_t i = 0 ; i < MAX_TAGDB_REQUESTS ; i++ ) 
		m_lists[i].freeList();
}

Tag *TagRec::getTag ( char *tagTypeStr ) {
	int32_t tagType = getTagTypeFromStr ( tagTypeStr );
	return getTag2 ( tagType );
}

Tag *TagRec::getTag2 ( int32_t tagType ) {
	Tag *tag = getFirstTag();
	// loop over all tags in the buf
	for ( ; tag ; tag = getNextTag ( tag ) ) {
		// skip if not a match
		if ( tag->m_type != tagType ) continue;
		// skip dups
		if ( tag->m_type == TT_DUP ) continue;
		// got it
		return tag;
	}
	// if not found return NULL
	return NULL;
}

// . functions to act on a site "tag buf", like that in Msg16::m_tagRec
// . first 2 bytes is size, 2nd to bytes is # of tags, then the tags
int32_t TagRec::getLong ( char        *tagTypeStr,
		       int32_t         defalt    , 
		       Tag        **bookmark  ,
		       int32_t        *timestamp ,
		       char       **user      ) {
	int32_t tagType = getTagTypeFromStr ( tagTypeStr );
	return getLong ( tagType   ,
			 defalt    ,
			 bookmark  ,
			 timestamp ,
			 user      );
}

int32_t TagRec::getLong ( int32_t         tagType   ,
		       int32_t         defalt    , 
		       Tag        **bookmark  ,
		       int32_t        *timestamp ,
		       char       **user      ) {
	// start here
	Tag *tag ;
	if ( ! bookmark ) tag = getFirstTag();
	else              tag = getNextTag ( *bookmark );
	// loop over all tags in the buf
	for ( ; tag ; tag = getNextTag ( tag ) ) {
		// skip if not a match
		if ( tag->m_type != tagType ) continue;
		// skip dups
		if ( tag->m_type == TT_DUP ) continue;
		// get the value as a int32_t
		int32_t score = 0;
		// the size
		char *data     = tag->getTagData();
		int32_t  dataSize = tag->getTagDataSize();
		//int32_t size = m_dataSize;
		// if ends in NULL trunc it
		if ( data[dataSize-1] == '\0' ) dataSize--;
		// trunc it
		//if ( size > 4 ) size = 4;
		// convert string to value, MUST be signed!!! the data
		// should inclue a \0
		score = atol2(data,dataSize);
		// if only a single byte.need to preserve negatives (twos comp)
		//if      ( size == 1 ) score = (int32_t)tag->m_data[0];
		//else if ( size == 2 ) score = (int32_t)*((int16_t *)tag->m_data);
		//else    gbmemcpy ( &score , tag->m_data , size );
		// bookmark, et al
		if ( bookmark  ) *bookmark  = tag;
		if ( timestamp ) *timestamp = tag->m_timestamp;
		if ( user      ) *user      = tag->getUser();
		return score;
	}
	// not found
	return defalt;
}

int64_t TagRec::getLongLong ( char        *tagTypeStr,
				int64_t    defalt    , 
				Tag        **bookmark  ,
				int32_t        *timestamp ,
				char       **user      ) {
	int32_t tagType = getTagTypeFromStr ( tagTypeStr );
	// start here
	Tag *tag ;
	if ( ! bookmark ) tag = getFirstTag();
	else              tag = getNextTag ( *bookmark );
	// loop over all tags in the buf
	for ( ; tag ; tag = getNextTag ( tag ) ) {
		// skip if not a match
		if ( tag->m_type != tagType ) continue;
		// skip dups
		if ( tag->m_type == TT_DUP ) continue;
		// get the value as a int32_t
		int64_t score = 0;
		// the size
		char *data     = tag->getTagData();
		int32_t  dataSize = tag->getTagDataSize();
		// if ends in NULL trunc it
		if ( data[dataSize-1] == '\0' ) dataSize--;
		// trunc it
		//if ( size > 8 ) size = 8;
		// now everything is a string
		score = atoll2(data,dataSize);
		// store it
		//gbmemcpy ( &score , tag->m_data , size );
		// bookmark, et al
		if ( bookmark  ) *bookmark  = tag;
		if ( timestamp ) *timestamp = tag->m_timestamp;
		if ( user      ) *user      = tag->getUser();
		return score;
	}
	// not found
	return defalt;
}

char *TagRec::getString ( char      *tagTypeStr,
			  char      *defalt    ,
			  int32_t      *size      ,
			  Tag      **bookmark  ,
			  int32_t      *timestamp ,
			  char     **user      ) {
	int32_t tagType = getTagTypeFromStr ( tagTypeStr );
	// start here
	Tag *tag ;
	if ( ! bookmark ) tag = getFirstTag();
	else              tag = getNextTag ( *bookmark );
	// loop over all tags in the buf
	for ( ; tag ; tag = getNextTag ( tag ) ) {
		// skip if not a match
		if ( tag->m_type != tagType ) continue;
		// skip dups
		if ( tag->m_type == TT_DUP ) continue;
		// want size? includes \0 probably
		if ( size      ) *size = tag->getTagDataSize();//m_dataSize;
		// bookmark, et al
		if ( bookmark  ) *bookmark  = tag;
		if ( timestamp ) *timestamp = tag->m_timestamp;
		if ( user      ) *user      = tag->getUser();
		// return it
		return tag->getTagData();//m_data;
	}
	// not found
	return defalt;
}

/*
// add a special tag with null m_data. this tells Msg9a to delete
// all tags of this tag type before adding any other tags of this type
// that we might have. it is basically a "negative" tag.
bool TagRec::addDelTag ( char *tagTypeStr ) {
	return addTag ( tagTypeStr ,
			0          , // timestamp
			NULL       , // user
			0          , // ip
			NULL       , // data
			0          );// dataSize
}

// returns false and sets g_errno on error
bool TagRec::addTag ( char        *tagTypeStr,
		      int32_t         timestamp , 
		      char        *user      ,
		      int32_t         ip        ,
		      char        *data      , 
		      int32_t         dataSize  ) {
	// get the tagType
	int32_t tagType = getTagTypeFromStr ( tagTypeStr );
	// breach check
	if ( dataSize + sizeof(Tag) > MAX_TAGREC_SIZE ) {
		g_errno = EBUFTOOSMALL; 
		return log("tagdb: no room to add tag");
	}
	// the Tag::m_dataSize is only 2 bytes... NOT ANYMORE, MDW
	if ( dataSize < 0 ) { // >= 65536 ) {
		g_errno = EBADENGINEER;
		return log("tagdb: tag dataSize of %"INT32" is >= 65536. "
			   "Bad value.",  dataSize);
	}
	// sanity check -- no binary chars allowed, must all be strings!
	// BUT they can have an empty string (i.e. just \0)
	if ( dataSize == 1 && data[0] < 9 && data[0] >= 0 && data[0] ) { 
		char *xx=NULL;*xx=0; }
	// make a tag
	char buf[MAX_TAGREC_SIZE];
	Tag *tag = (Tag *)buf;
	// fill it in
	tag->m_type      = tagType;
	tag->m_timestamp = timestamp;
	tag->m_ip        = ip;
	tag->m_dataSize  = dataSize;
	// dummy value for now
	tag->m_tagId     = 0;
	// careful!
	if ( sizeof(Tag) + dataSize + 10 > MAX_TAGREC_SIZE ) {
		g_errno = EBUFTOOSMALL;
		return log("tagdb: no room to add tag data");
	}
	// store user into special buffer
	int32_t ulen = 0;
	if ( user ) {
		ulen = gbstrlen(user);
		if ( ulen > 7 ) ulen = 7;
	}
	memset ( tag->m_user , 0    , 8    );
	gbmemcpy ( tag->m_user , user , ulen );
	// store data now too
	gbmemcpy ( tag->m_data , data , dataSize );
	// NULL terminate if they did not! now all tag are strings and must
	// be NULL terminated.
	if ( data && tag->m_data[dataSize-1] ) {
		tag->m_data[dataSize] = '\0';
		dataSize++;
		tag->m_dataSize++;
	}
	// the id is the hash for now (MDW)
	tag->m_tagId = hash32 ( (char *)tag,(int32_t)sizeof(tag)+dataSize , 0 );
	// 0 is not valid
	if ( tag->m_tagId == 0 ) tag->m_tagId = 1;
	// now add that tag
	return addTag ( tag );
}

// returns false and sets g_errno on error
bool TagRec::addTag ( Tag *TAG ) {
	// . do not allow empty user
	// . but "del tags" i.e. "negative tags" can have no user
	if ( TAG->m_dataSize>0 && (!TAG->m_user || TAG->m_user[0] == '\0') ) { 
		char *xx=NULL;*xx=0;}
	// sanity check
	if ( TAG->m_tagId == 0 ) { char *xx=NULL;*xx=0;}
	// come back up here if we did a remove operation
 loop:
	// start at the first tag
	Tag *tag = getFirstTag();
	// loop over all tags in the buf, see if we got a dup
	for ( ; tag ; tag = getNextTag ( tag ) ) {
		// skip if not matching id
		if ( tag->m_type != TAG->m_type ) continue;
		// skip if does not match user
		if ( memcmp(tag->m_user,TAG->m_user,7) ) continue;
		// data now has to match too, so we will allow tags of the
		// same type from the same user to be added if they have
		// different data now. i would only do this for strings,
		// but for int32_ts and chars i would skip this check...
		// so only replace "unique" tags of the same type.
		// mostly strings and embedded tag recs will be non-unquie
		if ( ! isTagTypeUnique ( tag->m_type ) ) {
			if ( tag->m_dataSize != TAG->m_dataSize ) continue;
			if ( memcmp(tag->m_data,TAG->m_data,tag->m_dataSize))
				continue;
		}
		// Msg8a allows multiple ST_SITE tags in order to indicate 
		// what sites the other tags came from (i.e. used by the 
		// inheritance loop below)
		// MDW: This is now covered by isTagTypeUnique() above.
		//if ( tag->m_type == ST_SITE ) continue;
		// it does match, so replace it!
		//removeTags ( tag->m_type , tag->m_user );
		removeTag ( tag );
		// start from the top
		goto loop;
	}
	// . ok, we "deduped" the tag
	// . point to the end of the buf
	char *p = getRecEnd();
	// get the max end
	char *pend = getMaxEnd();
	// how much do we need?
	int32_t need = TAG->getSize();
	// breach?
	if ( p + need > pend ) {
		char *site = getString("site","unknown");
		g_errno = EBUFTOOSMALL; 
		log("tagdb: no room to add tag to buf. tagtype=%s "
		    "tagsize=%"INT32" site=%s",  
		    getTagStrFromType ( TAG->m_type ) , need , site );
		//char *xx=NULL;*xx=0;
		return false;
	}
	// store it
	gbmemcpy ( p , TAG , need );
	// update our counters
	m_numTags++;
	m_dataSize += need;

	// SPECIAL: if it was ST_SITE, set our m_key, we are an Rdb record
	//if ( TAG->m_type != ST_SITE ) return true;
	if ( ! TAG->isType ("site") ) return true;

	// set the key
	Url u;
	// convenience
	char *site = TAG->m_data;
	int32_t  size = TAG->m_dataSize;
	// sanity check
	if ( site[size-1] != '\0' ) { char *xx=NULL;*xx=0; }
	// do not start with http:// ! wastes space!!
	if (size>=8 && strncmp(site,"http://",7)==0 ) {
		log("tagdb: don't sotre http:// in tags!");
		char *xx=NULL;*xx=0;
	}
	// do not include the NULL
	u.set ( site , size - 1 );
	// set our key, the endKey is our "startKey"
	m_key = g_tagdb.makeKey ( &u , false ); // isDelete?

	// success, return true
	return true;
}

bool TagRec::removeTags ( char *tagTypeStr , char *user , int32_t tagId ) {
	int32_t tagType = getTagTypeFromStr ( tagTypeStr );
	return removeTags ( tagType , user , tagId );
}

bool TagRec::removeTags ( int32_t tagType , char *user , int32_t tagId ) {
 loop:
	// start at the first tag
	Tag *tag = getFirstTag();
	// loop over all tags in the rec, see if we got a dup
	for ( ; tag ; tag = getNextTag ( tag ) ) {
		// id if matches, that is good enough
		if ( tagId && tag->m_tagId != tagId ) continue;
		// skip if not matching id
		if ( tagId == 0 && tag->m_type != tagType ) continue;
		// skip if does not match user
		if ( tagId == 0 && user && memcmp(tag->m_user,user,7))continue;
		// remove that tag
		removeTag ( tag );
		// re do loop
		goto loop;
	}
	// success
	return true;
}

bool TagRec::removeTag ( Tag *rmTag ) {
	// save this
	int32_t oldn = m_numTags;
	// start at the first tag
	Tag *tag = getFirstTag();
	// loop over all tags in the rec, see if we got a dup
	for ( ; tag ; tag = getNextTag ( tag ) ) {
		// must be it
		if ( tag != rmTag ) continue;
		// copy to here
		char *dst = (char *)tag;
		// size of tag we are removing
		int32_t size = tag->getSize();
		// from here
		char *src = dst + size;
		// end of tag buffer
		char *pend = getRecEnd();
		// byte to move
		int32_t move = pend - src;
		// it does match, so replace it!
		gbmemcpy ( dst , src , move );
		// decrement counts
		m_numTags--;
		m_dataSize -= size;
	}
	// sanity check
	if ( m_numTags != oldn - 1 ) { char *xx=NULL;*xx=0; }
	// success, return true
	return true;
}

// add all the tags from "tagRec" to our list of tags
bool TagRec::addTags ( TagRec *tagRec ) {

	// start at the first tag 
	Tag *tag = tagRec->getFirstTag();
	// . remove any tag of any of the tag types we got in "tagRec" ?
	// . deal with "negative" tags
	// . used by TagRec::addDelTag() above
	for ( ; tag ; tag = tagRec->getNextTag ( tag ) ) {
		// if tag has m_data, skip.
		if ( tag->m_data && tag->m_dataSize > 0 ) continue;
		// otherwise, it is a signal to nuke all tags of this type
		removeTags ( tag->m_type , NULL );
	}

	// start at the first tag again
	tag = tagRec->getFirstTag();
	// loop over all tags in the buf, see if we got a dup
	for ( ; tag ; tag = tagRec->getNextTag ( tag ) ) {
		// skip if it was a delete tag
		if ( tag->m_dataSize <= 0 ) continue;
		// do not transfer over ST_SITE tags if we already got one
		//if ( tag->m_type == ST_SITE && getTag ( ST_SITE ) ) continue;
		if ( tag->isType("site") && getTag("site") ) continue;
		// add it, return false on error, g_errno should be set
		if ( ! addTag ( tag ) ) return false;
	}
	return true;
}

// add all the tags from "tagRec" to our list of tags
bool TagRec::removeTags ( TagRec *tagRec ) {
	// start at the first tag
	Tag *tag = tagRec->getFirstTag();
	// loop over all tags in the buf, see if we got a dup
	for ( ; tag ; tag = tagRec->getNextTag ( tag ) ) {
		// do not remove ST_SITE tags
		//if ( tag->m_type == ST_SITE ) continue;
		if ( tag->isType("site") ) continue;
		// add it, return false on error, g_errno should be set
		if ( ! removeTags ( tag->m_type , tag->m_user ) ) return false;
	}
	return true;
}

Tag *TagRec::getNextTag ( Tag *tag ) {
	if ( m_numTags == 0 ) return NULL;
	if ( ! tag    ) return (Tag *)m_buf;
	char *tagEnd = getRecEnd();
	int32_t  size   = tag->getSize();
	char *ret    = ((char *)tag) + size;
	// overboard?
	if ( ret >= tagEnd ) return NULL;
	return (Tag *)ret;
}
*/

// return the number of tags having the particular TagType
int32_t TagRec::getNumTagTypes ( char *tagTypeStr ) {
	int32_t tagType = getTagTypeFromStr ( tagTypeStr );
	int32_t numTagType = 0;
	// start at the first tag
	Tag *tag = getFirstTag();
	// loop over all tags in the buf, see if we got a dup
	for ( ; tag ; tag = getNextTag ( tag ) ) {
		// skip dups
		if ( tag->m_type == TT_DUP ) continue;
		// if there is tagType match then increment the count
		if ( tag->m_type == tagType ) numTagType++;
	}
	return numTagType;
}

int32_t TagRec::getNumTags ( ) {
	int32_t numTags = 0;
	// start at the first tag
	Tag *tag = getFirstTag();
	// loop over all tags in the buf, see if we got a dup
	for ( ; tag ; tag = getNextTag ( tag ) )
		// skip dups
		if ( tag->m_type != TT_DUP ) numTags++;
	return numTags;
}

// . &tagtype%"INT32"=<tagtype>
// . &tagdata%"INT32"=<data>
// . &deltag%"INT32"=1 (to delete it)
// . set &user=mwells, etc. in cookie of HttpReqest, "r" for user
// . "this" TagRec's user, ip and timestamp will be carried over to "newtr"
// . returns false and sets g_errno on error
bool TagRec::setFromHttpRequest ( HttpRequest *r, TcpSocket *s ) {
	// clear it
	//reset();
	// get the username from the cookie
	//char *user = r->getStringFromCookie ( "username" , NULL );
	//char *user = g_users.getUsername ( r );
	// try from form
	//if ( ! user ) user = r->getString ("username",NULL);
	// if no user, don't bother!
	//if ( ! user ) {
	//	g_errno = EBADENGINEER;
	//	return log("tagdb: no username supplied for modifying tagdb.");
	//}
	// get the user ip address
	int32_t ip = 0;
	if ( s ) ip = s->m_ip;
	// get the time stamp
	int32_t now = getTimeGlobal();

	// . loop over all urls/sites in text area
	// . no! just use single url for now

	// put all urls in this buffer
	SafeBuf fou;

	// try from textarea if the ST_SITE was not in the tag section
	int32_t  uslen;
	char *us = r->getString("u",&uslen);
	if ( uslen <= 0 ) us = NULL;
	if ( us ) fou.safeMemcpy ( us , uslen );

	// read in file, file of urls
	int32_t ufuLen;
	char *ufu = r->getString("ufu",&ufuLen);
	if ( ufuLen <= 0 ) ufu = NULL;
	if ( us  ) ufu = NULL; // exclusive
	if ( ufu ) fou.fillFromFile ( ufu );

	// if st->m_urls has multiple urls, this "u" is not given in the
	// http request! but a filename is... and Msg9::addTags() should add
	// the ST_SITE field anyway...
	if ( ! ufu && ! us ) return true;

	// make it null terminated since we no longer do this automatically
	fou.pushChar('\0');

	// normalize it
	//Url u; u.set ( us , uslen );
	// point to it
	//char *site = u.getUrl();
	// skip http + ://
	//site += u.getSchemeLen() + 3; 
	// include the \0
	//int32_t psize = gbstrlen(p) + 1;

	// loop over all tags in the TagRec to mod them
	for ( int32_t i = 0 ; ; i++ ) {

		char buf[32];
		sprintf ( buf , "tagtype%"INT32"",i );
		char *tagTypeStr = r->getString(buf,NULL,NULL);
		// if not there we are done
		if ( ! tagTypeStr ) break;

		// should we delete it?
		sprintf ( buf , "deltag%"INT32"",i);
		char *deltag = r->getString(buf,NULL,NULL);
		//if ( deltag && deltag[0] ) continue;

		sprintf ( buf , "taguser%"INT32"",i);
		char *tagUser = r->getString( buf,NULL,"admin");//user);
		//if ( tagUser && tagUser[0]==0 ) tagUser = user;

		sprintf ( buf , "tagtime%"INT32"",i);
		int32_t  tagTime = r->getLong(buf,now);

		sprintf ( buf , "tagip%"INT32"",i);
		int32_t  tagIp   = r->getLong(buf,ip);

		// get the value of this tag
		sprintf ( buf , "tagdata%"INT32"" , i );
		char *dataPtr = r->getString ( buf , NULL );

		// get the tag original key
		key128_t key;
		sprintf ( buf , "tagn1key%"INT32"" , i );
		key.n1 = r->getLongLong ( buf, 0 );
		sprintf ( buf , "tagn0key%"INT32"" , i );
		key.n0 = r->getLongLong ( buf, 0LL );

		// for supporting dumping/adding of tagdb using wget
		sprintf ( buf , "tagn1key%"INT32"b" , i );
		int64_t v1 = r->getLongLong ( buf, key.n1 );
		sprintf ( buf , "tagn0key%"INT32"b" , i );
		int64_t v0 = r->getLongLong ( buf, key.n0 );
		bool hackKey = ( v1 || v0 );
		key.n1 = v1;
		key.n0 = v0;


		// if empty skip it
		if ( ! dataPtr    ) continue;
		if ( ! dataPtr[0] ) continue;
		// is it numeric? i think only ST_COMMENT is not
		//char isNum = true;
		// get the numeric
		//int32_t tagType = getTagTypeFromStr ( tagTypeStr );
		// set "isNum" to false if not numeric
		//if ( tagType == ST_COMMENT ) isNum = false;
		//if ( tagType == ST_SITE    ) isNum = false;
		//if ( tagType == ST_META    ) isNum = false;
		//if ( isTagTypeString ( tagType ) ) isNum = false;
		//int32_t  dataSize = 0;
		// . if it is a string, like ST_COMMENT
		// . include the \0
		//if ( ! isNum ) dataSize = gbstrlen(dataPtr) + 1;
		// everything is now a string
		int32_t dataSize = gbstrlen(dataPtr) + 1;
		// if numeric store in tag buf
		/*
		int64_t data;
		if ( isNum ) {
			data = atoll ( dataPtr );//r->getLongLong(val,-1);
			dataSize = 1;
			if ( data >= 0xffLL           ) dataSize = 2;
			if ( data >= 0xffffLL         ) dataSize = 3;
			if ( data >= 0xffffffLL       ) dataSize = 4;
			if ( data >= 0xffffffffLL     ) dataSize = 5;
			if ( data >= 0xffffffffffLL   ) dataSize = 6;
			if ( data >= 0xffffffffffffLL ) dataSize = 7;
			dataPtr = (char *)&data;
		}
		*/
		// add to tag buf
		//addTag ( tagTypeStr ,
		//	 tagTime  ,
		//	 tagUser  , 
		//	 tagIp    ,
		//	 dataPtr  ,
		//	 dataSize );


		// loop over all urls in the url file if provided
		char *up = fou.getBufStart();

		for ( ; ; ) {
			// set url
			char *urlPtr = up;
			// stop if EOF or processed the one url
			if ( ! urlPtr ) break;
			// advance it or NULL it out
			up = fou.getNextLine ( up );
			// null term the url ptr
			if ( up ) up[-1] = '\0';

			// save buffer spot in case we have to rewind
			int32_t saved = m_sbuf.length();

			// . add to tag rdb recs in safebuf
			// . this pushes the rdbid as first byte
			// . mdwmdwmdw
			Tag *tag = m_sbuf.addTag ( urlPtr, // us, // site ,
						   tagTypeStr ,
						   tagTime ,
						   tagUser ,
						   tagIp ,
						   dataPtr,
						   dataSize ,
						   RDB_TAGDB,
						   // do not push rdbid into safebuf
						   false ) ;
			// error?
			if ( ! tag )
				return false;

			// hack the key
			if ( hackKey ) // key.n1 != 0 || key.n0 != 0 ) 
				tag->m_key = key;

			bool deleteOldKey = false;

			// if tag has different key, delete the old one
			if ( key.n1 && tag->m_key != key ) deleteOldKey = true;
			
			// if del was marked, delete old one and do not add new one
			if ( deltag && deltag[0] ) {
				// rewind over the tag we were about to add
				m_sbuf.setLength ( saved );
				// and add as a delete
				deleteOldKey = true;
			}

			if ( deleteOldKey ) {
				// make it negative
				key128_t delKey = key;
				delKey.n0 &= 0xfffffffffffffffeLL;
				if (! m_sbuf.safeMemcpy((char *)&delKey,
							sizeof(key128_t)))
					return false;
			}
		}
	}


	// all done
	//if ( getTag ( ST_SITE ) ) return ;
	//if ( getTag("site") ) return;

	// add the special ST_SITE tag
	//addTag ( "site"  , // ST_SITE ,
	//	 now     ,
	//	 user    ,
	//	 ip      ,
	//	 p       ,
	//	 psize   );
	return true;
}

// to stdout
int32_t TagRec::print ( ) {
	SafeBuf sb;
	printToBuf ( &sb );
	// dump that
	return fprintf(stderr,"%s\n",sb.getBufStart());
}

bool TagRec::printToBuf (  SafeBuf *sb ) {
	Tag *tag = getFirstTag();
	//sb->safePrintf("k.n1=0x%08"XINT32" k.n0=0x%016"XINT64" version=%"INT32"",
	//	       m_key.n1,m_key.n0,(int32_t)m_version);
	for ( ; tag ; tag = getNextTag ( tag ) ) {
		if ( tag->m_type == TT_DUP ) continue; 
		tag->printToBuf ( sb );
		sb->pushChar('\n');
	}
	return true;
}

// . return size of characters scanned from "p"
// . returns 0 on error
/*
int32_t TagRec::setFromBuf ( char *p , char *pend ) {
	// remember the start
	char *start = p;
	// scan in the key
	//if ( strncmp(p,"k.n1=0x",7) != 0 ) return 0;
	// skip key stuff
	//p += 7;
	// clear our key
	//m_key.setToMin();
	// read in the key
	//key_t k;
	//sscanf(p,"k.n1=0x%08"XINT32" k.n0=0x%016"XINT64" ",&k.n1,&k.n0);

	// now do it the fast way and compare the results!
	//p += 7 ;
	//hexToBinary ( p , pend , ((char *)&m_key.n1)+3 , true );
	//p += 8 + 8;
	//hexToBinary ( p , pend , ((char *)&m_key.n0)+7 , true );
	// test it
	//if ( m_key.n1 != k.n1 || m_key.n0 != k.n0 ) { char *xx=NULL; *xx=0; }

	//p = strstr ( p , " version=");
	// error?
	//if ( ! p ) return 0;
	// skip " version="
	//p += 9;
	// get version
	//m_version = atoi(p);

	// skip p until space
	//while ( p < pend && *p != ' ' ) p++;
	// error?
	//if ( p >= pend ) return 0;
	// skip the space -- NO! tag parser wants the space
	//p++;

	// point to the where we should serialize the tags into
	//char *tagPtr = m_buf;

	char tbuf[5000];

	while ( p < pend ) {
		// now we should be pointing to the tag
		Tag *tag = (Tag *)tbuf;
		// serialize the tag from the buf
		int32_t asciiBytesRead = tag->setFromBuf ( p , pend );
		// if bad this is 0
		if ( asciiBytesRead == 0 ) return 0;
		// store tag into our safebuf. return 0 with g_errno set on err
		// . mdwmdwmdw
		if ( ! m_sbuf.addTag ( tag ) ) return 0;
		// point to next tag to read into our binary buffer
		//p += asciiBytesRead;
		// inc our ptr to point to next tag if it exists
		//tagPtr += tag->getSize();
		// inc our count in the TagRec
		//m_numTags++;
		// adjust our tag buffer size, TagRec::m_dataSize
		//m_dataSize = tagPtr - m_buf;
		// hey, it includes the other crap too!
		// it includes m_numTags + m_version, see Tagdb.h
		//m_dataSize += 2 + 1; 

	}

	// clear all lists
	//resetLists();
	// now make list point to that
	//m_lists[0].m_list     = m_sbuf.getBufStart();
	//m_lists[0].m_listSize = m_sbuf.length();
	//m_lists[0].m_listAllocSize = 0; // do not free it!
	//m_numLists = 0;

	//return getSize();
	return p - start;
}
*/

bool TagRec::setFromBuf ( char *p , int32_t bufSize ) {

	// assign to list! but do not free i guess
	m_lists[0].m_list = p;
	m_lists[0].m_listSize = bufSize;
	m_lists[0].m_listEnd = p + bufSize;
	m_lists[0].m_ownData = false;
	m_lists[0].m_lastKeyIsValid = false;
	m_lists[0].m_fixedDataSize = -1;
	m_lists[0].m_useHalfKeys = false;
	m_lists[0].m_ks = sizeof(key128_t);
	m_listPtrs[0] = &m_lists[0];
	m_numListPtrs = 1;

	return true;
}

bool TagRec::serialize ( SafeBuf &dst ) {
	Tag *tag = getFirstTag();
	for ( ; tag ; tag = getNextTag ( tag ) ) {
		if ( tag->m_type == TT_DUP ) continue;
		if ( ! dst.addTag ( tag ) ) return false;
	}
	return true;
}

bool TagRec::printToBufAsAddRequest ( SafeBuf *sb ) {
	Tag *tag = getFirstTag();
	for ( ; tag ; tag = getNextTag ( tag ) ) 
		if ( tag->m_type != TT_DUP ) tag->printToBufAsAddRequest ( sb);
	return true;
}

bool TagRec::printToBufAsXml ( SafeBuf *sb ) {
	Tag *tag = getFirstTag();
	for ( ; tag ; tag = getNextTag ( tag ) )
		if ( tag->m_type != TT_DUP ) tag->printToBufAsXml ( sb );
	return true;
}

bool TagRec::printToBufAsHtml ( SafeBuf *sb , char *prefix ) {
	Tag *tag = getFirstTag();
	for ( ; tag ; tag = getNextTag ( tag ) ) 
		if ( tag->m_type != TT_DUP ) tag->printToBufAsHtml (sb,prefix);
	return true;
}

bool TagRec::printToBufAsTagVector  ( SafeBuf *sb ) {
	Tag *tag = getFirstTag();
	for ( ; tag ; tag = getNextTag ( tag ) ) 
		if ( tag->m_type != TT_DUP ) tag->printToBufAsTagVector ( sb );
	return true;
}

Tag *TagRec::getTag ( char *tagTypeStr , char *dataPtr , int32_t dataSize ) {
	// get the tag type numerically
	int32_t tagType = getTagTypeFromStr ( tagTypeStr );
	Tag *tag = getFirstTag();
	for ( ; tag ; tag = getNextTag ( tag ) ) {
		// skip if tag does not match "tagType"
		if ( tag->m_type != tagType ) continue;
		// skip dup tags
		if ( tag->m_type == TT_DUP )  continue;
		// skip if dataSize does not match
		if ( tag->getTagDataSize() != dataSize ) continue;
		// skip if data does not match
		if ( memcmp ( tag->getTagData() , dataPtr , dataSize ) ) continue;
		// we got a match
		return tag;
	}
	return NULL;
}

//
// flags for a TagDescriptor
//

// is the tag a string type?
#define TDF_STRING 0x01
// can we have multiple tags of this type from the same user in the 
// same TagRec?
#define TDF_ARRAY  0x02
// . should we index it?
// . index gbtagjapanese:<score>
// . also index "gbtagjapanese" if score != 0
// . TODO: actually use this
#define TDF_NOINDEX  0x04

class TagDesc {
public:
	char *m_name;
	char  m_flags;
	// we compute the m_type of each TD on init
	int32_t  m_type;
};

// map the tags to names
static TagDesc s_tagDesc[] = {

	// data for the "lang" tag is 2 char language id followed by
	// a comma then a score from 1 to 100 to indicate percentage.
	// Allow multiple "lang" tags in one tagrec.
	{"rootlang"             ,TDF_STRING,0},

	// title tag and incoming link text of the root page is stored here
	// for determining default venue addresses
	{"roottitles"             ,TDF_STRING|TDF_NOINDEX,0},
	//{"rootlangid"             ,TDF_STRING|TDF_NOINDEX,0},

	// for addresses of the website, can be multiple
	{"venueaddress"             ,TDF_STRING|TDF_ARRAY|TDF_NOINDEX,0},

	/*
	{"langunknown"          ,0x00,0},
	{"english"              ,0x00,0},
	{"french"               ,0x00,0},
	{"spanish"              ,0x00,0},
	{"russian"              ,0x00,0},
	{"turkish"              ,0x00,0},
	{"japanese"             ,0x00,0},
	{"chinesetraditional"   ,0x00,0},
	{"chinesesimplified"    ,0x00,0},
	{"korean"               ,0x00,0},
	{"german"               ,0x00,0},
	{"dutch"                ,0x00,0},
	{"italian"              ,0x00,0},
	{"finnish"              ,0x00,0},
	{"swedish"              ,0x00,0},
	{"norwegian"            ,0x00,0},
	{"portuguese"           ,0x00,0},
	{"vietnamese"           ,0x00,0},
	{"arabic"               ,0x00,0},
	{"hebrew"               ,0x00,0},
	{"indonesian"           ,0x00,0},
	{"greek"                ,0x00,0},
	{"thai"                 ,0x00,0},
	{"hindi"                ,0x00,0},
	{"bengala"              ,0x00,0},
	{"polish"               ,0x00,0},
	{"tagalog"              ,0x00,0},
	*/

	/*
	{"spam"                 ,0x00,0},
	{"retail"               ,0x00,0},
	{"business"             ,0x00,0},
	{"adult"                ,0x00,0},
	{"forum"                ,0x00,0},
	{"blog"                 ,0x00,0},
	{"news"                 ,0x00,0},
	{"reference"            ,0x00,0},
	{"directory"            ,0x00,0},
	{"searchengine"         ,0x00,0},
	{"domainsquatter"       ,0x00,0},
	{"platform"             ,0x00,0},
	{"travel"               ,0x00,0},
	{"audio"                ,0x00,0},
	{"video"                ,0x00,0},
	{"socialnetworking"     ,0x00,0},
	*/

	{"manualban"            ,0x00,0},
	{"manualfilter"         ,0x00,0},
	// clock hashes are now stored in indexdb
	//{"clock"              ,0x00,0},
	{"dateformat"           ,0x00,0}, // 1 = american, 2 = european
	
	{"ruleset"              ,0x00,0},
	//{"filtered"             ,0x00,0},
	//{"compromised"          ,0x00,0},
	//{"good"                 ,0x00,0},
	{"deep"                 ,0x00,0},
	//{"quality"              ,0x00,0},
	//{"dmozcatid"            ,TDF_NOINDEX,0},
	{"comment"              ,TDF_STRING|TDF_NOINDEX,0},
	// we now index this. really we need it for storing into title rec.
	{"site"                 ,TDF_STRING|TDF_ARRAY,0},

	//{"meta"                 ,TDF_STRING,0},

	// . website contact info
	// . used by ContactInfo.cpp
	// . TDB_ARRAY means not to "overwrite" even if username is the same
	// . a website can have multiple street addresses, etc.
	// . the "lines" of an single street address are separated by ';'
	//   instead of \n to maintain tagdb dump output readability
	//{"streetaddress"        ,TDF_ARRAY,0},
	//{"phonenumber"          ,TDF_ARRAY,0},
	//{"faxnumber"            ,TDF_ARRAY,0},
	//{"emailaddress"         ,TDF_ARRAY,0},
	// . this tag can contain multiple zipcodes, separated by ' '
	// . we do index these for local search
	//{"zipcodes"             ,0x00,0},
	// . similar to zip codes, separated by ' '
	// . TODO: we need to fix Places.cpp to label the places for these tags
	//   but for now we can do gbtagstreetaddress:munich and hope for
	//   the best, although we will get websites on "munich st.!", but
	//   maybe you can combine that with gbtagstreetaddress:germany
	//{"countries",         ,0x00,0},
	//{"cities",            ,0x00,0},
	// this is "0" or "1". if it is "0" then the date lets XmlDoc.cpp know
	// when we last tried to get the contact info for the site
	{"hascontactinfo"       ,0x00,0},
	// street address using ; as delimeter
	{"contactaddress"              ,TDF_ARRAY|TDF_NOINDEX,0},
	{"contactemails"               ,TDF_ARRAY|TDF_NOINDEX,0},
	//{"emailaddressonsite"   ,TDF_ARRAY|TDF_NOINDEX,0},
	//{"emailaddressoffsite"  ,TDF_ARRAY|TDF_NOINDEX,0},
	{"hascontactform"       ,0x00,0},

	// subscribe to google's blacklist and mark the sites as this
	//{"malware"              ,0x00,0},

	// . this is used to define INDEPENDENT subsites
	// . such INDEPENDENT subsites should never inherit from this tag rec
	// . it is used to handle "homesteading" sites like geocities.com
	//   and the like, and is automatically set by SiteGetter.cpp
	// . if this is 1 then xyz.com/yyyyy/       is considered a subsite
	// . if this is 2 then xyz.com/yyyyy/zzzzz/ is considered a subsite
	// . if this is -1 then no subsite is found
	// . this should never be 0 either
	{"sitepathdepth"        ,0x00,0},

	// . used by XmlDoc::updateTagdb() and also used to determine
	//   if we should index a site in XmlDoc.cpp. to be indexed a site
	//   must be in google, or must have this tag type in its tag rec,
	//   or have some other, soon to be invented, tag
	// . really this is all controlled by url filters table
	// . allow multiple tags of this type from same "user"
	{"authorityinlink"      ,TDF_STRING|TDF_ARRAY,0},

	{"pagerank"             ,0x00,0},
	{"ingoogle"             ,0x00,0},
	{"ingoogleblogs"        ,0x00,0},
	{"ingooglenews"         ,0x00,0},

	// geo location from this news site directory
	{"abyznewslinks.address",0x00,0},

	// we now store site pop, etc. in tagdb
	{"sitenuminlinks"       ,0x00,0},
	{"sitenuminlinksuniqueip"  ,0x00,0},
	{"sitenuminlinksuniquecblock"  ,0x00,0},
	{"sitenuminlinkstotal"  ,0x00,0},

	// keep these although no longer used
	{"sitepop"  ,0x00,0},
	{"sitenuminlinksfresh"  ,0x00,0},


	// . the first ip we lookup for this domain
	// . this is permanent and should never change
	// . it is used by Spider.cpp to assign a host for throttling
	//   all urls/SpiderRequests from that ip
	// . so if we did change it then that would result in two hosts
	//   doing the throttling, really messing things up
	{"firstip"              ,0x00,0}



	/*
	{"user.id"              ,0x00,0},
	{"user.xml"             ,TDF_STRING,0},
	{"user.login"           ,TDF_STRING,0},
	{"user.password"        ,TDF_STRING,0},
	{"user.securityquestion",TDF_STRING,0},
	{"user.securityanswer"  ,TDF_STRING,0},
	{"user.email"           ,TDF_STRING,0},
	{"user.firstname"       ,TDF_STRING,0},
	{"user.lastname"        ,TDF_STRING,0},
	{"user.cookie"          ,TDF_STRING,0},
	{"user.zipcode"         ,TDF_STRING,0},
	{"user.city"            ,TDF_STRING,0},
	{"user.state"           ,TDF_STRING,0},
	{"user.imageurl"        ,TDF_STRING,0},

	{"user.dob"             ,TDF_STRING,0},
	{"user.language"        ,TDF_STRING,0},
	{"user.creditcardname"  ,TDF_STRING,0},
	{"user.creditcardnum"   ,TDF_STRING,0},
	{"user.creditcardexp"   ,TDF_STRING,0},
	{"user.creditcardcode"  ,TDF_STRING,0},
	{"user.lastlogin"       ,0x00,0},
	{"user.numlogins"       ,0x00,0},
	{"user.openlinksnewwin" ,0x00,0},
	{"user.usehttps"        ,0x00,0},
	{"user.maxreadhist"     ,0x00,0},
	{"user.maxsearchhist"   ,0x00,0},
	{"user.format"          ,0x00,0},
	{"user.acctbalance"     ,0x00,0},
	{"user.acctlimit"       ,0x00,0},
	{"user.acctsuspended"   ,0x00,0},
	{"user.acctbillemails"  ,TDF_STRING,0},
	{"user.adstopicid"      ,0x00,0},
	{"user.adsdailybudget"  ,0x00,0},
	{"user.adsdisabled"     ,0x00,0},
	{"user.feednumqueries"  ,0x00,0},
	{"user.feedcpq"         ,0x00,0},
	{"user.feeddailybudget" ,0x00,0},
	{"user.feeddisabled"    ,0x00,0},
	{"user.feedpassword"    ,TDF_STRING,0},
	{"user.feeddailycount"  ,TDF_ARRAY,0},
	{"user.usertransrec"    ,TDF_ARRAY,0},
	{"user.userhistoryrec"  ,TDF_ARRAY,0},
	{"user.userpanelrec"    ,TDF_ARRAY,0},
	{"trans.amount"         ,0x00,0},
	{"trans.desc"           ,TDF_STRING,0},
	{"hist.wasread"         ,0x00,0},
	{"hist.url"             ,TDF_STRING,0},
	{"hist.gigabits"        ,TDF_STRING,0},
	{"hist.timespent"       ,0x00,0},
	{"panel.topcid"         ,0x00,0},
	{"panel.showmainstream" ,0x00,0},
	{"panel.showblogs"      ,0x00,0},
	{"panel.showforum"      ,0x00,0},
	{"panel.showweb"        ,0x00,0},
	{"panel.showsearchbox"  ,0x00,0},
	{"panel.showimages"     ,0x00,0},
	{"panel.showvideo"      ,0x00,0},
	{"panel.showchatbox"    ,0x00,0},
	{"panel.showchatpics"   ,0x00,0},
	{"panel.chatboxnumlines",0x00,0},
	{"panel.popsliderval"   ,0x00,0},
	{"panel.agesliderval"   ,0x00,0},
	{"panel.windowxpos"     ,0x00,0},
	{"panel.windowypos"     ,0x00,0},
	{"panel.numstories"     ,0x00,0},
	{"panel.storylang"      ,TDF_STRING,0},
	{"panel.translatelang"  ,TDF_STRING,0},
	{"panel.displaylang"    ,TDF_STRING,0},
	{"panel.filterquery"    ,TDF_STRING,0},
	{"panel.sendemailalerts",TDF_STRING,0},
	{"chat.comment"         ,TDF_STRING,0},

	{"ad.topicid"           ,0x00,0},
	{"ad.userid"            ,0x00,0},
	{"ad.adid"              ,0x00,0},
	{"ad.title"             ,TDF_STRING,0},
	{"ad.text"              ,TDF_STRING,0},
	{"ad.url"               ,TDF_STRING,0},
	{"ad.keywordstring"     ,TDF_STRING,0},
	{"ad.dailypledge"       ,0x00,0},
	{"ad.disabled"          ,0x00,0},
	{"ad.dailyimpresscount" ,TDF_ARRAY,0},
	{"ad.dailyclickcount"   ,TDF_ARRAY,0}
	*/
};

// . convert "domain_squatter" to ST_DOMAIN_SQUATTER
// . used by CollectionRec::getRegExpNum()
// . tagnameLen is -1 if unknown
int32_t getTagTypeFromStr( char *tagname , int32_t tagnameLen ) {
	// this is now the hash
	int32_t tagType;
	if ( tagnameLen == -1 ) tagType = hash32n ( tagname );
	else                    tagType = hash32 ( tagname , tagnameLen );
	// make sure table is valid
	if ( ! s_initialized ) g_tagdb.setHashTable();
	// sanity check, make sure it is a supported tag!
	if ( ! s_ht.getValue ( &tagType ) ) { 
		log("tagdb: unsupported tagname \"%s\"",tagname);
		char *xx=NULL;*xx=0;
		return -1;
	}
	return tagType;
}

// . convert ST_DOMAIN_SQUATTER to "domain_squatter"
char *getTagStrFromType ( int32_t tagType ) {
	// make sure table is valid
	if ( ! s_initialized ) g_tagdb.setHashTable();
	TagDesc **ptd = (TagDesc **)s_ht.getValue ( &tagType );
	// sanity check
	if ( ! ptd ) { char *xx=NULL;*xx=0; }
	// return it
	return (*ptd)->m_name;
}

// a global class extern'd in .h file
Tagdb g_tagdb;
Tagdb g_tagdb2;

// a fake site for Tagdb::convert()
//Tagdb g_sitedb;

//static HashTableT<int64_t,int32_t> s_lockTable;
//static HashTableX s_lockTable2;

// reset rdb and Xmls
void Tagdb::reset() {
	m_rdb.reset();
	m_siteBuf1.purge();
	m_siteBuf2.purge();
	//s_lockTable2.reset();
}

bool Tagdb::setHashTable ( ) {
	if ( s_initialized ) return true;
	s_initialized = true;
	// the hashtable of TagDescriptors
	//if ( ! s_ht.set ( 1024 ) ) 
	if ( ! s_ht.set ( 4,sizeof(TagDesc *),1024,NULL,0,false,0,"tgdbtb" ) ) 
		return log("tagdb: Tagdb hash init failed.");
	// stock it
	int32_t n = (int32_t)sizeof(s_tagDesc)/(int32_t)sizeof(TagDesc);
	for ( int32_t i = 0 ; i < n ; i++ ) { 
		TagDesc *td = &s_tagDesc[i];
		char *s    = td->m_name;
		int32_t  slen = gbstrlen(s);
		// use the same algo that Words.cpp computeWordIds does 
		int32_t h = hash64Lower_a ( s , slen );
		// call it a bad name if already in there
		TagDesc **petd = (TagDesc **)s_ht.getValue ( &h );
		if ( petd )
			return log("tagdb: Tag %s collides with old tag %s",
				   td->m_name,(*petd)->m_name);
		// set the type
		td->m_type = h;
		// add it
		s_ht.addKey ( &h , &td );
	}
	return true;
}

bool Tagdb::init ( ) {
	// snity test
	//if ( TAGREC_CURRENT_VERSION >= 30 ) {
	//	log("tagdb: fix call to convert()");
	//	char *xx = NULL; *xx = 0; 
	//}

	// force it now
	g_conf.m_tagdbMaxTreeMem = 101028000;

	// . what's max # of tree nodes?
	// . assume avg tagdb rec size (siteUrl) is about 82 bytes we get:
	// . NOTE: 32 bytes of the 82 are overhead
	int32_t maxTreeNodes = g_conf.m_tagdbMaxTreeMem  / 82;

	//int64_t pcmem = 250000000; // 250MB
	// TODO: make it a biased disk page cache!
	int64_t pcmem = 160000000; // 160MB
	// turn it off for rebuilding posdb, to 10MB anyway
	pcmem = 10000000;
	//int32_t pcmem = 100000000;
	// each entry in the cache is usually just a single record, no lists,
	// unless a hostname has multiple sites in it. has 24 bytes more 
	// overhead in cache.
	//int32_t maxCacheNodes = g_conf.m_tagdbMaxCacheMem / 106;
	// we now use a page cache
	// if ( ! m_pc.init ("tagdb",RDB_TAGDB,pcmem,GB_TFNDB_PAGE_SIZE))
	// 	return log("tagdb: Tagdb init failed.");

	// init this
	//if ( ! s_lockTable2.set(8,4,32,NULL,0,false,0,"taglocktbl") )
	//	return log("tagdb: lock table init failed.");

	// . initialize our own internal rdb
	// . i no longer use cache so changes to tagdb are instant
	// . we still use page cache however, which is good enough!
	return m_rdb.init ( g_hostdb.m_dir               ,
			    "tagdb"                     ,
			    true                       , // dedup same keys?
			    -1                         , // fixed record size
			    -1,//g_conf.m_tagdbMinFilesToMerge   ,
			    g_conf.m_tagdbMaxTreeMem  ,
			    maxTreeNodes               ,
			    // now we balance so Sync.cpp can ordered huge list
			    true                        , // balance tree?
			    0 , //g_conf.m_tagdbMaxCacheMem ,
			    0 , //maxCacheNodes              ,
			    false                      , // half keys?
			    false                      , //m_tagdbSaveCache
			    NULL,//&m_pc                      ,
			    false,  // is titledb
			    true ,  // preload disk page cache
			    sizeof(key128_t),     // key size
			    true ); // bias disk page cache?
}

bool Tagdb::init2 ( int32_t treeMem ) {
	// . what's max # of tree nodes?
	// . assume avg tagdb rec size (siteUrl) is about 82 bytes we get:
	// . NOTE: 32 bytes of the 82 are overhead
	int32_t maxTreeNodes = treeMem / 82;
	// . initialize our own internal rdb
	// . i no longer use cache so changes to tagdb are instant
	// . we still use page cache however, which is good enough!
	return m_rdb.init ( g_hostdb.m_dir               ,
			    "tagdbRebuild"               ,
			    true                       , // dedup same keys?
			    -1                         , // fixed record size
			    50,//g_conf.m_tagdbMinFilesToMerge   ,
			    treeMem ,
			    maxTreeNodes               ,
			    // now we balance so Sync.cpp can ordered huge list
			    true                        , // balance tree?
			    0 , //g_conf.m_tagdbMaxCacheMem ,
			    0 , //maxCacheNodes              ,
			    false                      , // half keys?
			    false                      , //m_tagdbSaveCache
			    NULL , // pc
			    false,  // is titledb
			    false ,  // preload disk page cache
			    sizeof(key128_t),     // key size
			    false ); // bias disk page cache?
}

/*
bool Tagdb::addColl ( char *coll, bool doVerify ) {
	if ( ! m_rdb.addColl ( coll ) ) return false;
	if ( ! doVerify ) return true;//false;
	// verify
	if ( verify(coll) ) return true;
	// if not allowing scale, return false
	//if ( ! g_conf.m_allowScale ) return false;
	// otherwise let it go
	//log ( "tagdb: Verify failed, but scaling is allowed, passing." );
	//return true;
	return false;
}
*/


bool Tagdb::verify ( char *coll ) {
	char *rdbName = NULL;
	rdbName = "Tagdb";
	
	log ( LOG_DEBUG, "db: Verifying %s for coll %s...", rdbName, coll );
	
	g_threads.disableThreads();

	Msg5 msg5;
	Msg5 msg5b;
	RdbList list;
	key128_t startKey;
	key128_t endKey;
	startKey.setMin();
	endKey.setMax();
	CollectionRec *cr = g_collectiondb.getRec(coll);
	
	if ( ! msg5.getList ( RDB_TAGDB    ,
			      cr->m_collnum          ,
			      &list         ,
			      (char *)&startKey      ,
			      (char *)&endKey        ,
			      64000         , // minRecSizes   ,
			      true          , // includeTree   ,
			      false         , // add to cache?
			      0             , // max cache age
			      0             , // startFileNum  ,
			      -1            , // numFiles      ,
			      NULL          , // state
			      NULL          , // callback
			      0             , // niceness
			      false         , // err correction?
			      NULL          ,
			      0             ,
			      -1            ,
			      true          ,
			      -1LL          ,
			      &msg5b        ,
			      true          )) {
		g_threads.enableThreads();
		return log("tagdb: HEY! it did not block");
	}

	int32_t count  = 0;
	int32_t got    = 0;
	//int32_t numOld = 0;
	for ( list.resetListPtr() ; ! list.isExhausted() ;
	      list.skipCurrentRecord() ) {
		//key128_t k = list.getCurrentKey();
		key128_t k;
		list.getCurrentKey ( &k );
		// skip negative keys
		if ( (k.n0 & 0x01) == 0x00 ) continue;
		count++;
		// see if it is the "old" school tagdb rec
		//char *data       = list.getCurrentData();
		//int32_t  dataSize = list.getCurrentDataSize();
		// this is the file number in the old school tagdb recs
		// and it is the version number in the new school style recs.
		// just make sure the new school version number stays below 30!
		//char  version  = *data;
		// lower 3 bytes are the file number. >= 30 on gk
		//if ( version >= 30 ) numOld++;
		//uint32_t groupId = g_tagdb.getGroupId ( &k );
		uint32_t shardNum = getShardNum ( RDB_TAGDB , &k );
		if ( shardNum == getMyShardNum() ) got++;
	}
	if ( got != count ) {
		// tally it up
		g_rebalance.m_numForeignRecs += count - got;
		log ("tagdb: Out of first %"INT32" records in %s, only %"INT32" belong "
		     "to our group.",count,rdbName,got);
		// exit if NONE, we probably got the wrong data
		if ( got == 0 ) log("tagdb: Are you sure you have the "
					   "right "
					   "data in the right directory? "
					   "Exiting.");
		log ( "tagdb: Exiting due to %s inconsistency.", rdbName );
		g_threads.enableThreads();
		return g_conf.m_bypassValidation;
	}
	log ( LOG_DEBUG, "db: %s passed verification successfully for %"INT32" "
	      "recs.",rdbName, count );

	// turn threads back on
	g_threads.enableThreads();

	// if no recs in tagdb, but sitedb exists, convert it
	if ( count > 0 ) return true;

	// . convert them
	// . returns false and sets g_errno on error
	//if ( ! convert ( coll ) ) return false;

	// DONE
	g_threads.enableThreads();
	return true;
}

/////////////
//
// past blast -- for Tagdb::convert()
//
////////////
/*
struct SiteType {
	SiteType() : m_score(0) {}
	SiteType& operator=(SiteType& o) 
	{m_type=o.m_type;m_score=o.m_score; return *this;}
	// get this type's size
	int32_t getStoredSize() { 
		if (isType4Bytes(m_type)) return sizeof(m_type)+4;
		else                      return sizeof(m_type)+1;
	};
	enum {
		FIRST_TYPE = 0,
		SPAM = FIRST_TYPE,   //probablitity that it is spam
		RETAIL,     //selling something
		BUSINESS,   //a corporate storefront eg ibm.com
		ADULT,      //not safe for kids, higher score = more hardcore
		FORUM,      //message board
		BLOG,       //or personal home page
		NEWS,       //articles, opinions magazines
		REFERENCE,  //all special interest sites
		DIRECTORY,  //links organized categorically
		SEARCH_ENGINE, //indexed info
		DOMAIN_SQUATTER,
		PLATFORM,   //political candidate, or org
		TRAVEL,     //Travel sites
		AUDIO,   //podcast, streaming radio
		VIDEO,   //flash video
		SOCIAL_NETWORKING,//dating, myspace, facebook
		MANUAL_BAN,  //a human hates this site
		PAGE_RANK,  //google's page rank
		CLOCK1_PREHASH,   //hash of unique preceeding 1st clock
		CLOCK1_PREHASH_CNT, // count of tags to make 1st clock hash
		DATE_FORMAT,    //format of dates on page
		CLOCK2_PREHASH,   //hash of unique tags preceeding 2nd clock
		CLOCK2_PREHASH_CNT, // count of tags to make 2nd clock hash
		CLOCK3_PREHASH,   //hash of unique tags preceeding 3rd clock
		CLOCK3_PREHASH_CNT, // count of tags to make 3rd clock hash
		CLOCK4_PREHASH,   //hash of unique tags preceeding 4th clock
		CLOCK4_PREHASH_CNT, // count of tags to make 4th clock hash

		// ....ADD ALL NEW TYPES HERE... corruption upon ye if not

		LAST_TYPE,
		BAD_TYPE = LAST_TYPE,

		TOTAL_TYPE_COUNT = (LAST_TYPE-FIRST_TYPE)
	};
	// . types can be 1 byte or 4 bytes. if they are 4 bytes, they must be
	//   added to this function
	static bool isType4Bytes(int type) {
		if ( type == CLOCK1_PREHASH ) return true;
		if ( type == CLOCK2_PREHASH ) return true;
		if ( type == CLOCK3_PREHASH ) return true;
		if ( type == CLOCK4_PREHASH ) return true;
		return false;
	}

	static int32_t getScoreSize(uint8_t type) {
		if ( type == CLOCK1_PREHASH ) return 4;
		if ( type == CLOCK2_PREHASH ) return 4;
		if ( type == CLOCK3_PREHASH ) return 4;
		if ( type == CLOCK4_PREHASH ) return 4;
		return 1;
	};
	bool isNormScore() {return m_type <= PAGE_RANK;}
	uint8_t   m_type;
	uint32_t  m_score;
};

// . convert the old Tagdb format into the new format
bool Tagdb::convert ( char *coll ) {

	g_threads.disableThreads();

	log("db: Trying to convert sitedb for coll %s into tagdb",coll);
	collnum_t collnum = g_collectiondb.getCollnum ( coll );
	// open up old sitedb files
	int32_t mem          = 100000000;
	int32_t maxTreeNodes = mem  / 82;
	//Rdb sitedb;
	g_sitedb.m_rdb.init ( g_hostdb.m_dir ,
			      "sitedb"       ,
			      true           , // dedup same keys?
			      -1             , // fixed record size
			      9999           , // MinFilesToMerge
			      100000000      , // g_conf.m_tagdbMaxTreeMem
			      maxTreeNodes   ,
			      true           , // balance tree?
			      0              , // g_conf.m_tagdbMaxCacheMem
			      0              , // maxCacheNodes
			      false          , // half keys?
			      false          , // m_tagdbSaveCache
			      NULL           , // DiskPageCache *, &m_pc
			      false          , // is titledb
			      false          , // preload disk page cache
			      12             , // key size
			      false          );// bias disk page cache?
	//g_collectiondb.init(true);
	g_sitedb.addColl ( coll, false );

	Msg5 msg5;
	Msg5 msg5b;
	RdbList list;
	key_t startKey;
	key_t endKey;
	startKey.setMin();
	endKey.setMax();
	key_t k;
	bool threadsWereEnabled = !g_threads.areThreadsDisabled();
	g_threads.disableThreads();

 loop:
	// loop over all tagdb recs in tagdb
	if ( ! msg5.getList ( RDB_SITEDB    ,
			      coll          ,
			      &list         ,
			      startKey      ,
			      endKey        ,
			      64000         , // minRecSizes   ,
			      true          , // includeTree   ,
			      false         , // add to cache?
			      0             , // max cache age
			      0             , // startFileNum  ,
			      -1            , // numFiles      ,
			      NULL          , // state
			      NULL          , // callback
			      0             , // niceness
			      false         , // err correction?
			      NULL          ,
			      0             ,
			      -1            ,
			      true          ,
			      -1LL          ,
			      &msg5b        ,
			      true          )) {
		if(threadsWereEnabled) g_threads.enableThreads();
		return log("db: HEY! it did not block");
	}

	int32_t count  = 0;
	for ( list.resetListPtr() ; ! list.isExhausted() ;
	      list.skipCurrentRecord() ) {
		k = list.getCurrentKey();
		count++;
		char *data     = list.getCurrentData();
		//int32_t  dataSize = list.getCurrentDataSize();
		// point to end of it
		//char *pend        = data + dataSize;
		// parse the old site rec
		char *p           = data;
		int32_t  old_sfn     = (*(int32_t *)p) & 0x00ffffff;
		//char  old_version = p[3];
		p +=  4;
		char *old_site    = p;
		int32_t  old_siteLen = gbstrlen(p);
		p +=  old_siteLen + 1;
		int32_t  old_time    = *(int32_t *)p;
		p +=  4;
		char *old_comment = p;
		p +=  gbstrlen(p) + 1;
		//char *old_username = p;
		p +=  gbstrlen(p) + 1;
		//unsigned char siteFlags = *p;
		p += 1;
		//char siteQuality = *p;
		p += 1;
		//char    incHere  = *(int32_t    *)p;
		uint8_t numTypes = *(uint8_t *)p;
		p += 1;

		// do not start with http:// ! wastes space!!
		if (old_siteLen>=8 && strncmp(old_site,"http://",7)==0 ) {
			old_site    += 7;
			old_siteLen -= 7;
		}
		// sanity check
		//Url s; s.set ( old_site, old_siteLen );
		//key_t newk = g_tagdb.makeKey ( &s , false );
		//if ( k != newk ) { char *xx=NULL;*xx=0; }
		// . without any tags, what is our dataSize?
		// . version(1 byte)+site(X bytes)+NULLTerm(1 byte)+
		//   #Tags(2 bytes)
		//int32_t dataSize2 = 1 + old_siteLen + 1 + 2;
		// set the new rec with this stuff
		TagRec newgr;
		//newgr.set ( k                      ,
		//	    dataSize2              ,
		//	    TAGREC_CURRENT_VERSION ,
		//	    old_site               );
		int32_t now = getTimeGlobal();
		// add the "site" name as a tag (include NULL)
		newgr.addTag ( ST_SITE , old_time , "conv" , 0, 
			       old_site, gbstrlen(old_site)+1);
		// the banned tag
		if ( old_sfn == 30 ) {
			char data = 1;
			newgr.addTag ( ST_MANUAL_BAN ,now, "conv", 0,&data,1);
		}
		if ( old_sfn == 50 ) {
			char data = 1;
			newgr.addTag ( ST_DEEP,now, "conv", 0,&data,1);
		}
		// just for historical reasons, keep this too
		newgr.addTag ( ST_RULESET , now , "conv",0,(char *)&old_sfn,1);
		// . add in comment tag
		// . this will increase newgr::m_dataEnd/m_dataSize
		// . include NULL
		if ( old_comment[0] )
			newgr.addTag ( ST_COMMENT  ,now, "conv", 0,
				       old_comment , gbstrlen(old_comment)+1);
		// reset these
		bool gotPrehash1 = false;
		bool gotPrehash2 = false;
		bool gotPrehash3 = false;
		bool gotPrehash4 = false;
		bool gotPrehashCount1 = false;
		bool gotPrehashCount2 = false;
		bool gotPrehashCount3 = false;
		bool gotPrehashCount4 = false;
		int32_t prehash1;
		int32_t prehash2;
		int32_t prehash3;
		int32_t prehash4;
		char prehashCount1;
		char prehashCount2;
		char prehashCount3;
		char prehashCount4;
		// now for the old SiteTypes
		for ( int32_t i = 0 ; i < numTypes ; i++ ) {
			//while ( p < pend ) {
			//SiteType *ost = (SiteType *)p;
			// get the type
			char siteType = *p; p++;
			// and the score
			char *siteTypeScore = p;
			int32_t  siteTypeScoreSize = 
				SiteType::getScoreSize(siteType);
			p += siteTypeScoreSize;
			// a 0 score in the old sitedb meant to ignore
			if ( *siteTypeScore == 0 && siteTypeScoreSize == 1 )
				continue;
			// map the siteType 1-1 for the most part
			int32_t tagType = siteType + ST_SPAM;
			// if the type is SiteType::CLOCK2-4_ re-map it
			if ( siteType == SiteType::CLOCK1_PREHASH ) {
				gotPrehash1 = true;
				prehash1 = *(int32_t *)siteTypeScore;
				continue;
			}
			if ( siteType == SiteType::CLOCK1_PREHASH_CNT ) {
				gotPrehashCount1 = true;
				prehashCount1 = *(char *)siteTypeScore;
				continue;
			}
			if ( siteType == SiteType::CLOCK2_PREHASH ) {
				gotPrehash2 = true;
				prehash2 = *(int32_t *)siteTypeScore;
				continue;
			}
			if ( siteType == SiteType::CLOCK2_PREHASH_CNT ) {
				gotPrehashCount2 = true;
				prehashCount2 = *(char *)siteTypeScore;
				continue;
			}
			if ( siteType == SiteType::CLOCK3_PREHASH ) {
				gotPrehash3 = true;
				prehash3 = *(int32_t *)siteTypeScore;
				continue;
			}
			if ( siteType == SiteType::CLOCK3_PREHASH_CNT ) {
				gotPrehashCount3 = true;
				prehashCount3 = *(char *)siteTypeScore;
				continue;
			}
			if ( siteType == SiteType::CLOCK4_PREHASH ) {
				gotPrehash4 = true;
				prehash4 = *(int32_t *)siteTypeScore;
				continue;
			}
			if ( siteType == SiteType::CLOCK4_PREHASH_CNT ) {
				gotPrehashCount4 = true;
				prehashCount4 = *(char *)siteTypeScore;
				continue;
			}
			// but DATE_FORMAT is off
			if ( siteType == SiteType::DATE_FORMAT )
				tagType = ST_DATE_FORMAT;

			// panic
			if ( tagType >= ST_LAST_TAG ) {
				log("db: got bad tagtype %"INT32" for sitedb rec.",
				    (int32_t)tagType);
				continue;
			}
			// add to new rec
			newgr.addTag ( tagType             , // should be 1-1
				       now               ,
				       "conv"            ,
				       0                 , // ip
				       siteTypeScore     ,
				       siteTypeScoreSize );
		}
		// add in the clock stuff
		if ( gotPrehash1 && gotPrehashCount1 ) {
			// make a 5 byte thingy
			char tmp[5];
			tmp[0] = prehashCount1;
			gbmemcpy ( tmp+1 , &prehash1, 4 );
			newgr.addTag ( ST_CLOCK,now,"conv",0,tmp,5);
		}
		if ( gotPrehash2 && gotPrehashCount2 ) {
			// make a 5 byte thingy
			char tmp[5];
			tmp[0] = prehashCount2;
			gbmemcpy ( tmp+1 , &prehash2, 4 );
			newgr.addTag ( ST_CLOCK,now,"conv",0,tmp,5);
		}
		if ( gotPrehash3 && gotPrehashCount3 ) {
			// make a 5 byte thingy
			char tmp[5];
			tmp[0] = prehashCount3;
			gbmemcpy ( tmp+1 , &prehash3, 4 );
			newgr.addTag ( ST_CLOCK,now,"conv",0,tmp,5);
		}
		if ( gotPrehash4 && gotPrehashCount4 ) {
			// make a 5 byte thingy
			char tmp[5];
			tmp[0] = prehashCount4;
			gbmemcpy ( tmp+1 , &prehash4, 4 );
			newgr.addTag ( ST_CLOCK,now,"conv",0,tmp,5);
		}

		// now the langs
		uint8_t numLangs = *p;
		p += 1;
		for ( int32_t i = 0 ; i < numLangs ; i++ ) {
			uint8_t langId = *p;
			p += 1;
			int32_t score = (int32_t)*(uint8_t *)p;
			p += 1;
			// add to new rec
			newgr.addTag ( langId , // should be 1-1
				       now    ,
				       "conv" ,
				       0      , // ip
				       (char *)&score ,
				       1      );
		}

		// print it out
		SafeBuf sb;
		newgr.printToBuf(&sb);
		logf(LOG_INFO,"tagdb: %s",sb.getBufStart());
		
		Rdb *r = &g_tagdb.m_rdb;
	
		// . add the new site rec back as a TagRec
		// . it should overwrite the old one since the key is the same
		// . this should not block
		// . it should do a dump if tree is full
		if ( ! r->addRecord ( collnum              ,
				      newgr.getKey     ()  ,
				      newgr.getData    ()  ,
				      newgr.getDataSize()  ,
				      MAX_NICENESS         )) {
			log("tagdb: convert: %s",mstrerror(g_errno));
			char *xx=NULL;*xx=0;
		}

		// do a blocking dump of tree if it's 90% full now
		if (r->m_mem.is90PercentFull() || r->m_tree.is90PercentFull()){
			log("tagdb: convert: dumping tree to disk.");
			if ( ! r->dumpTree ( 0 ) ) // niceness
				return log("tagdb: convert: dump failed.");
		}
	}

	// if list not empty, get more
	if ( list.isEmpty() ) { g_threads.enableThreads(); return true; }
	// advance startKey
	startKey = k;
	startKey += 1;
	// watch for wrap, that means done, too
	if ( startKey < k ) { g_threads.enableThreads(); return true; }
	// otherwise, do more
	goto loop;
}
*/

/*
// . dddddddd dddddddd dddddddd dddddddd  d = domain hash w/o collection
// . uuuuuuuu uuuuuuuu uuuuuuuu uuuuuuuu  u = url hash
// . uuuuuuuu uuuuuuuu uuuuuuuu uuuuuuuu  
key_t Tagdb::makeKey ( Url *u , bool isDelete ) {
	key_t k;
	// hash full hostname
	k.n1 = hash32 ( u->getHost() , u->getHostLen() );
	// set lower 64 bits of key to hash of this url
	k.n0 = hash64 ( u->getUrl() , u->getUrlLen() );
	// clear low bit if we're a delete, otherwise set it
	if ( isDelete ) k.n0 &= 0xfffffffffffffffeLL;
	else            k.n0 |= 0x0000000000000001LL;
	return k;
}
*/

// . ssssssss ssssssss ssssssss ssssssss  hash of site/url
// . xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx  tagType OR hash of that+user+data
// . xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx
key128_t Tagdb::makeStartKey ( char *site ) { // Url *u ) {
	key128_t k;
	// hash full hostname
	//k.n1 = hash64 ( u->getHost() , u->getHostLen() );
	k.n1 = hash64n ( site );
	//k.n1 = hash32 ( u->getUrl(), u->getUrlLen() );
	//k.n1 = hash32 ( u->getDomain(), u->getDomainLen() );
	// set lower 64 bits of key to hash of this url
	k.n0 = 0;
	return k;
}

key128_t Tagdb::makeEndKey ( char *site ) { //  Url *u ) {
	key128_t k;
	// hash full hostname
	//k.n1 = hash64 ( u->getHost() , u->getHostLen() );
	k.n1 = hash64n ( site );
	//k.n1 = hash32 ( u->getUrl(), u->getUrlLen() );
	//k.n1 = hash32 ( u->getDomain(), u->getDomainLen() );
	// set lower 64 bits of key to hash of this url
	k.n0 = 0xffffffffffffffffLL;
	return k;
}

key128_t Tagdb::makeDomainStartKey ( Url *u ) {
	key128_t k;
	// hash full hostname
	k.n1 = hash64 ( u->getDomain() , u->getDomainLen() );
	//k.n1 = hash32 ( u->getUrl(), u->getUrlLen() );
	//k.n1 = hash32 ( u->getDomain(), u->getDomainLen() );
	// set lower 64 bits of key to hash of this url
	k.n0 = 0;
	return k;
}

key128_t Tagdb::makeDomainEndKey ( Url *u ) {
	key128_t k;
	// hash full hostname
	k.n1 = hash64 ( u->getDomain() , u->getDomainLen() );
	//k.n1 = hash32 ( u->getUrl(), u->getUrlLen() );
	//k.n1 = hash32 ( u->getDomain(), u->getDomainLen() );
	// set lower 64 bits of key to hash of this url
	k.n0 = 0xffffffffffffffffLL;
	return k;
}


/*
// . returns 0 if "url" is not a suburl of "site"
// . otherwise, returns "percent" of "url" that matches "site"
int32_t Tagdb::getMatchPoints ( Url *recUrl , Url *url ) {
	// reset pts to 0
	int32_t pts = 0;
	
	// temporary fix to the hostname key collision problem is Tagdb Rdb
	int32_t  rhlen = recUrl->getHostLen ();

	char *uhost = url   ->getDomain    ();
	int32_t  uhlen = url   ->getDomainLen ();
	char *shost = recUrl->getDomain    ();
	int32_t  shlen = recUrl->getDomainLen ();
	//int32_t  uip   = url->getIp       ();
	//int32_t  sip   = site->getIp      ();

	// MDW: we are not really doing ips like this now
	if ( uhlen != shlen || strncmp( uhost, shost, uhlen ) != 0 )
	//	if ( ! uip || uip != sip ) return 0;
		return 0;
		
	// compare ports for bonus points
	// but return 0 if site's port is not default
	int32_t  rport  = recUrl->getPort   ();
	int32_t  uport  = url->getPort      ();
	if ( rport == uport ) pts += 1000000;
	else if ( uport != url->getDefaultPort() ) return 0;

	// now ensure url's path is a subpath of recUrl's
	int32_t  rplen = recUrl->getPathLen();
	char *rpath = recUrl->getPath();
	int32_t  uplen = url->getPathLen();
	char *upath = url->getPath();
	if ( rplen > uplen                          ) return 0;
	if ( strncmp ( upath , rpath , rplen ) != 0 ) return 0;
	// . now we got a solid match
	// . add 1 pt for each char in recUrl's path
	// . so the longer recUrl's path the better the match (more specific)
	// . this allows us to override TagRecs for deeper sub urls
	pts += rplen;
	// add in host size of the matching recUrl
	pts += rhlen*1000;
	// all done
	return pts;
}
*/

///////////////////////////////////////////////
//
// for getting the final TagRec for a url
//
///////////////////////////////////////////////

Msg8a::Msg8a() {
	m_replies  = 0;
	m_requests = 0;
}

Msg8a::~Msg8a ( ) {
	reset();
}
	
void Msg8a::reset() {
	// do no free if in progress, reply may come in and corrupt the mem
	if ( m_replies != m_requests && ! g_process.m_exiting ) { 
		char *xx=NULL;*xx=0; }
	//for ( int32_t i = 0 ; i < m_replies ; i++ ) 
	//	m_lists[i].reset();
	m_replies  = 0;
	m_requests = 0;
}

// . get records from multiple subdomains of url
// . calls g_udpServer.sendRequest() on each subdomain of url
// . all matching records are merge into a final record
//   i.e. site tags are also propagated accordingly
// . closest matching "site" is used as the "site" (the site url)
bool Msg8a::getTagRec ( Url   *url , 
			// site of the url
			char  *site ,
			//char  *coll             , 
			collnum_t collnum,
			bool   skipDomainLookup , // useCanonicalName ,
			int32_t   niceness         ,
			void  *state            ,
			void (* callback)(void *state ),
			TagRec *tagRec ,
			bool   doInheritance ,
			char   rdbId ) {


	CollectionRec *cr = g_collectiondb.getRec ( collnum );
	if ( ! cr ) { 
		g_errno = ENOCOLLREC;
		return true;
	}
	
	// reset tag rec
	tagRec->reset();//m_numListPtrs = 0;

	// sanity check
	if ( rdbId != RDB_TAGDB ) {char *xx=NULL;*xx=0;}
	// save it
	m_rdbId = rdbId;

	// in use? need to wait before reusing
	if ( m_replies != m_requests ) {char *xx=NULL;*xx=0; }
	// then we gotta free the lists if any
	reset();

	m_niceness = niceness;
	//m_coll     = coll;
	m_collnum = collnum;
	m_tagRec   = tagRec;
	m_callback = callback;
	m_state    = state;
	//m_url      = url;
	// reset
	m_errno    = 0;
	m_requests = 0;
	m_replies  = 0;
	m_doneLaunching = false;
	//m_doFullUrl = true;
	//m_skipDomainLookup = skipDomainLookup;

	// set siteLen to the provided site if it is non-NULL
	int32_t siteLen = 0;
	if ( site ) siteLen = gbstrlen(site);

	// . get the site
	// . msge0 passes this in as NULL an expects us to figure it out
	// . if site was NULL that means we guess it. default to hostname
	//   unless in a recognized for like /~mwells/
	if ( ! site || siteLen <= 0 ) {
		SiteGetter sg;
		sg.getSite ( url->getUrl() ,
			     NULL , // tagrec
			     0 , // timestamp
			     collnum, // coll
			     m_niceness,
			     NULL, // state
			     NULL); // callback
		// if it set it to a recognized site, like ~mwells
		// then set "site"
		if ( sg.m_siteLen ) {
			site    = sg.m_site;
			siteLen = sg.m_siteLen;
		}
	}

	// if provided site was NULL and not of a ~mwells type of form
	// then default it to hostname
	if ( ! site || siteLen <= 0 ) {
		site    = url->getHost();
		siteLen = url->getHostLen();
	}

	// if still the host is bad, then forget it
	if ( ! site || siteLen <= 0 ) {
		log("tagdb: got bad url with no site");
		m_errno = EBADURL;
		g_errno = EBADURL;
		return true;
	}

	// temp null terminate it
	char c = site[siteLen];
	site[siteLen] = '\0';

	// use that
	m_siteStartKey = g_tagdb.makeStartKey ( site );//url );
	m_siteEndKey   = g_tagdb.makeEndKey   ( site ); // url );

	// un NULL terminate it
	site[siteLen] = c;




	// ignore this part of url is already root like
	//if ( m_url->isRoot() ) m_doFullUrl = false;

	// makeStartKey only works on the hostname of the url, so doing the
	// full url has no effect right now
	//m_doFullUrl = false;

	// sendPageInject keeps "url" on the stack!
	//m_url.set ( url->getUrl() , url->getUrlLen() );
	m_url = url;
	

	// save this
	m_doInheritance = doInheritance;
	// . launch a request for each subdomain of the url
	// . the request format is 
	// . <url>\0<niceness><coll>\0
	// . that way we can use a small request buffer and have different
	//   pointers to the different subdomains
	//char *p = m_request;
	// point to url
	char *u    = url->getUrl();
	int32_t  ulen = url->getUrlLen();
	// point to the TLD of the url
	char *tld  = url->getTLD();
	// . if NULL, that is bad... TLD is unsupported
	// . no! it could be an ip address!
	// . anyway, if the tld does not exist, just return an empty tagrec
	//   do not set g_errno
	if ( ! tld && ! url->isIp() ) return true;
	//if ( ! tld ) { g_errno = EBADURL; return true; }
	// url cannot have NULLs in it because handleRequest8a() uses
	// gbstrlen() on it to get its size
	for ( int32_t i = 0 ; i < ulen ; i++ ) {
		if ( u[i] ) continue;
		log("TagRec: got bad url with NULL in it %s",u);
		m_errno = EBADURL;
		g_errno = EBADURL;
		return true;
	}
	// skip over http://
	int32_t  plen = url->getSchemeLen() + 3;
	u    += plen;
	ulen -= plen;
	// copy over url without the protocol thingy (http://)
	//gbmemcpy ( p , u , ulen ); 
	// get the domain
	m_dom = url->getDomain();
	// if none, bad!
	if ( ! m_dom && ! url->isIp() ) return true;
	// save this
	//m_host = url->getHost();
	// get its delta
	//int32_t delta = dom - u;
	// . save ptr for launchGetRequests()
	// . move this BACKWARDS for subdomains that have a ton of .'s
	// . no, now move towards domain
	m_p = m_url->getHost();
	// and save this too
	m_hostEnd = m_url->getHost() + m_url->getHostLen();
	// if ip just use the full "hostname" which is the full ip address
	//if ( url->isIp() ) m_p = m_host;

	// launch the requests
	if ( ! launchGetRequests() ) return false;
	// . they did it without blocking
	// . this sets g_errno on error
	gotAllReplies();
	// did not block
	return true;
}

// . returns false if blocked, true otherwise
// . sets g_errno and returns true on error
bool Msg8a::launchGetRequests ( ) {
	// clear it
	g_errno = 0;
	bool tryDomain = false;
 loop:
	// return true if nothing to launch
	if ( m_doneLaunching ) return (m_requests == m_replies);
	// don't bother if already got an error
	if ( m_errno ) return (m_requests == m_replies);
	// limit max to 5ish
	if (m_requests >=MAX_TAGDB_REQUESTS) return (m_requests==m_replies);
	// take a breath
	QUICKPOLL(m_niceness);

	// . first, try it by canonical domain name
	// . if that finds no matches, then try it by ip domain
	// get host
	//char *subdom    = m_p;
	//int32_t  subdomLen = m_hostEnd - m_p;

	key128_t startKey ;
	key128_t endKey   ;
	//int32_t     siteHash32;
	// . if our first time, do the full url!
	// . need to do this because the turking process (XmlDoc::getTurkForm()
	//   and PageReindex.cpp:processTurkForm()) add tags to tagdb based on
	//   the full url.
	/*
	if ( m_doFullUrl ) {
		startKey = g_tagdb.makeStartKey ( m_url );
		endKey   = g_tagdb.makeEndKey   ( m_url );
		// . like the "norm" url above
		// . we'll get back a list of tags for this hostname,
		//   but they could all be from different sites, some sites
		//   would be the hostname, other tags might be from sites
		//   that are a subsite of the hostname, so we have to make
		//   sure the tag's key.n0 matches this siteHash32
		siteHash32 = hash32 ( m_url->getUrl() , m_url->getUrlLen());
	}
	else {
		// make into a url
		Url u;
		u.set ( subdom , subdomLen );
		// set key range now
		startKey = g_tagdb.makeStartKey ( &u );
		endKey   = g_tagdb.makeEndKey   ( &u );
		// . like the "norm" url above
		// . we'll get back a list of tags for this hostname,
		//   but they could all be from different sites, some sites
		//   would be the hostname, other tags might be from sites
		//   that are a subsite of the hostname, so we have to make
		//   sure the tag's key.n0 matches this siteHash32
		siteHash32 = hash32 ( u.getUrl() , u.getUrlLen() );
	}
	*/

	if ( tryDomain ) {
		startKey = g_tagdb.makeDomainStartKey ( m_url );
		endKey   = g_tagdb.makeDomainEndKey   ( m_url );
		if ( g_conf.m_logDebugTagdb )
			log("tagdb: looking up domain tags for %s",
			    m_url->getUrl());
	}
	else {
		// usually the site is the hostname but sometimes it is like
		// "www.last.fm/user/breendaxx/"
		//startKey = g_tagdb.makeStartKey ( m_site );//url );
		//endKey   = g_tagdb.makeEndKey   ( m_site ); // url );
		startKey = m_siteStartKey;
		endKey   = m_siteEndKey;
		if ( g_conf.m_logDebugTagdb )
			log("tagdb: looking up site tags for %s",
			    m_url->getUrl());
	}
		

	// get the groupid
	//uint32_t groupId = g_tagdb.getGroupId ( startKey );

	// get the next mcast
	Msg0 *m = &m_msg0s[m_requests];
	// and the list
	RdbList *listPtr = &m_tagRec->m_lists[m_requests];

	// bias based on the top 64 bits which is the hash of the "site" now
	//uint32_t gid = g_hostdb.getGroupId ( m_rdbId , &startKey , true );
	//Host *group = g_hostdb.getGroup ( gid );
	int32_t shardNum = getShardNum ( m_rdbId , &startKey );//, true );
	Host *firstHost ;
	// if niceness 0 can't pick noquery host.
	// if niceness 1 can't pick nospider host.
	firstHost = g_hostdb.getLeastLoadedInShard ( shardNum , m_niceness );
	int32_t firstHostId = firstHost->m_hostId;

	// . launch this request, even if to ourselves
	// . TODO: just use msg0!!
	bool status = m->getList ( firstHostId     , // hostId
				   0          , // ip
				   0          , // port
				   0          , // maxCacheAge
				   false      , // addToCache
				   m_rdbId, //RDB_TAGDB  ,
				   m_collnum     ,
				   listPtr    ,
				   (char *) &startKey  ,
				   (char *) &endKey    ,
				   10000000            , // minRecSizes
				   this                , // state
				   gotMsg0ReplyWrapper ,
				   m_niceness          ,
				   true                , // error correction?
				   true                , // include tree?
				   true                , // doMerge?
				   firstHostId         , // firstHostId
				   0                   , // startFileNum
				   -1                  , // numFiles
				   3600*24*365         );// timeout
	// all done?
	//if ( m_p == m_url->getDomain() ) m_doneLaunching = true;
	// error?
	if ( status && g_errno ) {
		// g_errno should be set, we had an error
		m_errno = g_errno;
		return (m_requests == m_replies);
	}
	// successfully launched
	m_requests++;
	// if we got a reply instantly
	if ( status ) m_replies++;

	if ( ! tryDomain ) { //&& 
	     //! m_skipDomainLookup &&
	     //m_url->getHostLen() != m_url->getDomainLen() ) {
		tryDomain = true;
		goto loop;
	}

	//
	// no more looping!
	//
	// i don't think we need to loop any more because we got all the
	// tags for this hostname. then the lower bits of the Tag key
	// corresponds to the actual SITE hash. so we gotta filter those
	// out i guess after we read the whole list.
	//
	return (m_requests == m_replies);
	//m_doneLaunching = true;
	//goto loop;

	/*
	// do not advance m_p if doing the full url first
	if ( m_doFullUrl ) {
		m_doFullUrl = false;
		goto loop;
	}
	// . advance m_p
	// . we go backwards to better support subdomains that have a ton
	//   of periods in them...
	for ( ; m_p < m_dom && *m_p != '.' ; m_p++ );
	// advance over .
	if ( m_p != m_dom ) m_p++;
	// if another dot that is bad!
	if ( *m_p == '.' ) m_errno = EBADURL;
	// launch another
	goto loop;
	*/
}
	
void gotMsg0ReplyWrapper ( void *state ) {
	Msg8a *THIS = (Msg8a *)state;
	// we got one
	THIS->m_replies++;
	// error?
	if ( g_errno ) THIS->m_errno = g_errno;
	// launchGetRequests() returns false if still waiting for replies...
	if ( ! THIS->launchGetRequests() ) return;
	// get all the replies
	THIS->gotAllReplies();
	// set g_errno for the callback
	if ( THIS->m_errno ) g_errno = THIS->m_errno;
	// otherwise, call callback
	THIS->m_callback ( THIS->m_state );
}

// get the TagRec from the reply
void Msg8a::gotAllReplies ( ) {
	// if any had an error, don't do anything
	if ( m_errno ) return;
	// scan the lists
	for ( int32_t i = 0 ; i < m_replies ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get list
		RdbList *list = &m_tagRec->m_lists[i];
		// skip if empty
		if ( list->m_listSize <= 0 ) continue;
		// panic msg
		if ( list->m_listSize >= 10000000 ) {
			log("tagdb: CAUTION!!! cutoff tagdb list!");
			log("tagdb: CAUTION!!! will lost useful info!!");
			char *xx=NULL;*xx=0;
		}
		// otherwise, add to array
		m_tagRec->m_listPtrs[m_tagRec->m_numListPtrs] = list;
		// advance
		m_tagRec->m_numListPtrs++;
	}

	// . now scan all the tags for this HOSTNAME
	// . filter out tags that are not for a supersite of our url
	// . i.e. if our url is www.xyz.com/tim/bob/file.html
	//   then hash
	//   http://www.xyz.com/
	//   http://www.xyz.com/tim/
	//   http://www.xyz.com/tim/bob/
	//   and skip over any tag whose lower 32 bits does not match
	//   one of those hashes...
	// . see where we set Tag::m_key.n0 in Tag::set() above:
	//   m_key.n0 |= (uint32_t) hash32 ( norm.getUrl(),norm.getUrlLen() );
	//   where "norm" is the provided site but with a http:// in front
	//   and a / at the end since Url::set() normalized it
	// . m_url is the url we want to get the tags for
	// . HACK: right now just restrict to the hostname!
	/*
	Url norm;
	norm.set ( m_url->getHost() , m_url->getHostLen() );
	uint32_t siteHash32 = hash32 ( norm.getUrl(),norm.getUrlLen() );
	// . and the domain too so we can ban domains
	// . this is messed up because we can't just hash the domain, we have
	//   to hash it like a complete url because that is what Tag::set()
	//   does when it makes the key's top 32 bits.
	uint32_t siteHash32d = 0;
	int32_t conti = 0;
	siteHash32d = hash32_cont ( "http://",7,siteHash32d,&conti);
	siteHash32d = hash32_cont ( norm.getDomain(),
				    norm.getDomainLen(),
				    siteHash32d,
				    &conti);
	siteHash32d = hash32_cont ( "/",1,siteHash32d,&conti);
	// the non-del bit i guess. we forgot to shift up when we made
	// the key above!
	siteHash32  |= 0x01;
	siteHash32d |= 0x01;
	*/

	// scan tags in list and set Tag::m_type to TT_DUP if its a dup
	Tag *tag = m_tagRec->getFirstTag();
	HashTableX cx;
	char cbuf[2048];
	cx.set ( 4,0,64,cbuf,2048,false,m_niceness,"tagtypetab");
	// . loop over all tags in all lists in order by key
	// . each list should be from a different suburl?
	// . the first list should be the narrowest/longest?
	for ( ; tag ; tag = m_tagRec->getNextTag ( tag ) ) {
		// breathe
		QUICKPOLL(m_niceness);

		// skip tag if it is not from the proper site. we are
		// only guarenteed that all tags in this list are for the
		// same HOSTNAME not SITE! site is in the lower bits
		// of the tagdb key.
		// should fix www.paypal.com:1234 bug where we were reading
		// sitenuminlinks from that tag and was always 0!! even
		// when we'd add a count of 2k to the www.paypal.com site...
		// now filter out www.paypal.com:1234's tags!
		// TODO: allow multiple different siteHash32 values to match
		// here, use one siteHash32 for each possible suburl of "m_url"
		// so if m_url is "http://www.xyz.com/tim/" then we also
		// can match hash32("http://www.xyz.com/tim/" not just
		// "http://www.xyz.com/" which is how it is now.
		//uint32_t th32 = tag->m_key.n0 & 0xffffffff;
		//if ( th32 != siteHash32 && th32 != siteHash32d ) { 
		//	// maybe use TT_DIFFSITE instead of this! TODO!
		//	tag->m_type = TT_DUP;
		//	continue;
		//}

		// form the hash!
		uint32_t h32 = (uint32_t)((tag->m_key.n0) >> 32);
		// skip if not unique
		//if ( ! isTagTypeUnique ( tag->m_type ) ) continue;
		// otherwise, record it
		if ( cx.isInTable(&h32 ) ) // tag->m_type) ) 
			tag->m_type = TT_DUP;
		else if ( ! cx.addKey(&h32) ) {
			m_errno = g_errno;
			return;
		}
	}
}		

/*
// get the TagRec from the reply
void TagRec::gotAllReplies ( ) {
	// if any had an error, don't do anything
	if ( m_errno ) return;
	// time how long this takes and log it
	int64_t startTime = gettimeofdayInMilliseconds();
	// how many TagRecs we matched
	int32_t n = 0;
	// arrays for pointing to best matching TagRecs
	//char *data       [128];
	//int32_t  dataSizes  [128];
	//int32_t  dataScores [128];
	char *recs      [128];
	int32_t  recScores [128];

	// . each reply is a list of TagRecs
	// . each TagRec is a standard Rdb record
	// . key|dataSize|data...
	// . go through all TagRecs and sort our list of ptrs to the
	//   best TagRecs
	// . some TagRecs will not even match, so do not include those in
	//   our list of pointers
	// . the closest matching TagRecs will be on top
	// . inherit Tags from lesser matching TagRecs provided there
	//   is no such Tag::m_type from a closer matching TagRec
	// . if xyz.com is banned and abc.xyz.com has a 0 score for the
	//   ST_BANNED Tag, then it is effectively "unbanned" and should
	//   not inherit the score from xyz.com for ST_BANNED.
	// . so by scanning each TagRec in order, we compose our own
	//   final merged TagRec that may have a lot more Tags in it
	//   than any one matching TagRec
	for ( int32_t i = 0 ; i < m_replies ; i++ ) {
		// get the list from this reply
		RdbList *list = &m_lists[i];
		// scan list
		for ( ; ! list->isExhausted() ; list->skipCurrentRecord() ) {
			// break if overflow
			if ( n >= 128 ) break;
			// get next rec
			//char *d     = list->getCurrentData    ();
			//int32_t  dsize = list->getCurrentDataSize();
			char *rec = list->getCurrentRec();
			// set TagRec to it
			TagRec *gr = (TagRec *)rec;
			// get the site
			//char *site = gr->getString(ST_SITE,NULL);
			char *site = gr->getString("site",NULL);
			// sanity check
			if ( ! site ) { char *xx=NULL;*xx=0; }
			// make it a url
			Url u;
			u.set ( site , gbstrlen(site) );
			// score it
			int32_t s = g_tagdb.getMatchPoints ( &u , m_url );
			// skip it if not a match
			if ( s <= 0 ) continue;
			// save it
			//data       [n] = d;
			//dataSize   [n] = dsize;
			recs      [n] = rec;
			recScores [n] = s;
			n++;
		}
	}

	// if no recs, we did not match anything
	if ( n == 0 ) return;
	// or on error
	if ( m_errno ) return;

	// bubble sort the recs by their scores, highest score first
 bubble:
	bool swapped = false;
	for ( int32_t i = 1 ; i < n ; i++ ) {
		// keep going if in correct order
		if ( recScores[i-1] >= recScores[i] ) continue;
		// swap
		char *t1 = recs      [i-1];
		int32_t  t2 = recScores [i-1];
		recs      [i-1] = recs      [i];
		recs      [i  ] = t1;
		recScores [i-1] = recScores [i];
		recScores [i  ] = t2;
		swapped = true;
	}
	if ( swapped ) goto bubble;

	// parse the best matching SiteData
	//TagRec gr ; gr.set ( data[0] , dataSizes[0] );
	// use the site from the best matching TagRec as our site
	//m_siteUrl.set ( gr.getSite() , gr.getSiteLen() );

	// reset the inheritance array
	//char array[ST_LAST_TAG];
	//memset ( array , -1 , 256 );
	HashTable ia;
	char ibuf [ 1024 * 8 ];
	ia.set ( 1024 , ibuf , 1024 * 8 );

	// we just store the tags, ptrs into the tags in the m_lists
	//Tag *tags[MAX_TAGS];
	// assume we got no tags
	//int32_t numTags = 0;
	// size of all tags
	//int32_t size = 0;

	// set our new tag rec
	m_tagRec->reset();

	// . only get tags from the first matching tag rec if we should not
	//   do the inheritance loop
	// . if they click "get rec" on PageTagdb, then do not do inheritance,
	//   but if they click "get tags", then do it!
	if ( ! m_doInheritance && n > 0 ) n = 1;


	// . DO NOT INHERIT ANYTHING FROM TAG RECS that have a sitePathDepth 
	//   tag in them UNLESS the sitePathDepth does not work on us
	// . i.e. if xyz.com has a sitePathDepth of 2 in its TagRec and the
	//   url we are looking at is xyz.com/a/b/c/d then we must assume that
	//   out site is xyz.com/a/b/ we are an independent subsite of 
	//   xyz.com and inherit nothing from it
	SiteGetter siteGetter;

	// site getter sometimes adds recs to tagdb to add in a new subsite
	// it finds... i'd imagine this will create a parsing inconsistency
	// when injecting docs into the "qatest123" coll... but oh well!
	int32_t timestamp = getTimeGlobal();

	// . begin the "inheritance loop"
	// . fill our m_tags[] array with the Tags that apply to us
	for ( int32_t i = 0 ; i < n ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// parse the TagRec (very fast)
	        TagRec *gr = (TagRec *)recs[i];
		// is "url" an independent subsite of gr's site?
		char *us = m_url->getUrl();
		bool st=siteGetter.getSite(us,gr,timestamp,m_coll,m_niceness );
		// sanity check, not allowed to block since state is NULL!
		if ( ! st ) { char *xx=NULL;*xx=0; }
		// are we independent subsite? if so, do not inherit
		// from that. this is used to prevent www.geocities.com/~mark/
		// from gaining the benefits of being on the www.geocities.com
		// site. TODO later: we should make another tag to indicate
		// a subsite is expicitly independent. but for now we rely
		// on the "sitepathdepth" tag automatically computed by 
		// SiteGetter.cpp.
		//if ( siteGetter.isIndependentSubsite() ) continue;

		// 
		// TODO:
		// NONO, just do not inherit sitenumlinks or any tag
		// that is marked as such!!! add a new flag to the tags!!!!!!
		//

		// always add the ST_SITE tag first from each tag so we know 
		// what site the other tags belong to
		//Tag *stag = gr->getTag ( ST_SITE );
		Tag *stag = gr->getTag ( "site" );
		// only add if non null
		if ( stag ) m_tagRec->addTag ( stag );
		// last tag
		Tag *last = NULL;
		// loop over all tags in TagRec #i
	tagLoop:
		// get the tag id of current tag
		Tag *tag = gr->getNextTag ( last );
		// assign
		last = tag;
		// was that the end of the tags? if so, go to next TagRec
		if ( ! tag ) continue;
		// get tag id
		int32_t tagType = tag->m_type;
		// skip all ST_SITE tags, we added those first above
		//if ( tagType == ST_SITE ) goto tagLoop;
		if ( tag->isType("site") ) goto tagLoop;
		// sanity check
		//if ( tagType >= ST_LAST_TAG ) { char *xx=NULL;*xx=0;}
		// for getting the next tag, remember this
		last = tag;
		// . have we added this yet?
		// . if tagType added from a prev TagRec do not "inherit" it
		//if(array[tagType] != -1 && array[tagType] != i) goto tagLoop;
		int32_t slot = ia.getSlot ( tagType );
		if ( slot >= 0 && ia.getValueFromSlot(slot) != i) goto tagLoop;

		// if tag type is "eventtag" then only add it if the site of this
		// tagrec EQUALS our url. exact match... that way we make sure to only
		// tag a single url, otherwise we might accidentally tag an entire site.
		if ( tag->isType("eventtag") ) {
			// must be in tagRec that matches us the closest
			if ( i != 0 ) goto tagLoop;
			// if no site, skip it
			if ( ! stag ) goto tagLoop;
			// and even then must match site exactly
			char *site = stag->m_data;
			// as string
			char *url  = m_url->getUrl();
			int32_t  ulen = m_url->getUrlLen();
			// skip our proto (http://)
			url  += m_url->getSchemeLen() + 3;
			ulen -= m_url->getSchemeLen() + 3;
			// remove trailing /
			if ( ulen > 0 && url[ulen-1] == '/' ) ulen--;
			// likewise for site
			int32_t slen = gbstrlen(site);
			if ( slen > 0 && site[slen-1] == '/' ) slen--;
			// skip if not exact
			if ( slen != ulen ) goto tagLoop;
			// compare, must match exactly, if not, do not add tag
			if ( strncmp(url,site,slen) != 0 ) goto tagLoop;
		}

		// ok, add/inherit it
		//tags[numTags++] = tag;
		// add it directly to m_tagRec
		if ( ! m_tagRec->addTag ( tag ) ) {
			log("tagdb: addTag failed: %s",mstrerror(g_errno));
			m_errno = g_errno;
			break;
		}
		// add in size
		//size += tag->getSize();
		// note it, so we do not add/inherit it from another TagRec
		//array[tagType] = i;
		ia.addKey ( tagType , i );
		// add more tags
		goto tagLoop;
	}

	// sanity!
	//if ( size > 32000                   ) { char *xx=NULL;*xx=0; }
	//if ( size + 2 + 2 > MAX_TAGREC_SIZE ) { char *xx=NULL;*xx=0; }
	// then copy the tags into the buffer
	//for ( int32_t i = 0 ; i < numTags ; i++ )
	//	m_tagRec->addTag ( tags[i] );

	// sanity check
	//if ( p - m_tagRec > MAX_TAGREC_SIZE ) { char *xx=NULL;*xx=0;}

	// free the mem
	reset();
		
	// time it
	int64_t took = gettimeofdayInMilliseconds() - startTime;
	if(took>10) log(LOG_INFO, "admin: gotreply for msg8a took %"INT64"",took);
}
*/
/*
///////////////////////////////////////////////
//
// Msg9a : for modifying TagRecs in Tagdb
//
///////////////////////////////////////////////

Msg9a::Msg9a () { 
	m_requestBuf = NULL;
	m_requests   = 0;
	m_replies    = 0;
}
Msg9a::~Msg9a() { reset(); }

void Msg9a::reset() {
	// guard against not waiting for all replies to come in
	if ( m_requests != m_replies && ! g_process.m_exiting ) {
		char *xx=NULL;*xx=0; }
	if ( ! m_requestBuf ) return;
	mfree ( m_requestBuf , m_requestBufSize , "msg9a" );
	m_requestBuf = NULL;
}

// . returns false if blocked, true otherwise
// . sets errno on error
// . "urls" is a NULL-terminated list of space-separated urls
// . if "addTags" is true, then the tags in "tagRec" will be added to the
///  the TagRecs specified by the sites in "sites". if a TagRec
//   does not exist for a given "site" then it will be added just
//   so we can add the Tags to it. If it does exist, we will
//   just append the given Tags to it.
// . to "delete" a tag, just assign it a dataSize of 0!
// . Tags added with the same user name and tag type of an existing tag 
//   will overwrite it.
// . you can now optionally supply an array of ptrs to sites, sitePtrs.
// . you can call this with your "tagRec" on the stack because we copy
//   its contents into our own buffer here
bool Msg9a::addTags ( char    *sites                  ,
		      char   **sitePtrs               ,
		      int32_t     numSitePtrs            ,
		      char    *coll                   , 
		      void    *state                  ,
		      void   (*callback)(void *state) ,
		      int32_t     niceness               ,
		      TagRec  *tagRec                 ,
		      bool     nukeTagRecs            ,
		      int32_t    *ipVector               ) {

	// incase we are being re-used!
	reset();

	g_errno = 0;

	// sanity check, one or the other
	if ( sites && sitePtrs ) { char *xx=NULL;*xx=0; }

	// ipVector only used with sitePtrs for now
	if ( ! sitePtrs && ipVector ) { char *xx=NULL;*xx=0; }

	// when we add the "site" tag to it use the timestamp from one
	// of the tags we are adding... therefore we must require there be
	// some tags! we do this to insure injection consistency into the
	// "qatest123" collection.
	if ( ! tagRec || tagRec->getNumTags() <= 0 ) { char *xx=NULL;*xx=0; }

	// use the first timestamp
	int32_t timestamp = tagRec->getFirstTag()->m_timestamp;

	// . up to 20 oustanding Msg0 getting the exact TagRec for each site
	// . when we get it we immediately modify it and then add it back
	//   using Msg4.
	// . to resolve collisions we could assign a particular hostid
	//   to handle adding each site... yeah, how about the local host.
	// . so forward the Msg9a add/del/rpl request to the responsible
	//   host. then it can lock the "site" until the add completes.
	// . it should use Msg1 to add it.

	// reset
	m_errno    = 0;
	m_requests = 0;
	m_replies  = 0;
	m_niceness = niceness;
	m_state    = state;
	m_callback = callback;

	int32_t collLen = gbstrlen(coll);

	// how many urls in the sites do we have?
	int32_t numUrls = 0;
	// point to buf
	char *s = sites;
	// count each one
	while ( sites && *s ) {
		// skip whitespace
		while ( *s && is_wspace_a(*s) ) s++;
		// alnum?
		if ( *s ) numUrls++;
		// skip url
		while ( *s && ! is_wspace_a(*s) ) s++;
	}
	if ( sitePtrs )
		numUrls = numSitePtrs;


	// how much buf do we need to hold all the requests for all the sites
	int32_t need = 0;


	// just a buffer of sites
	if ( sites ) 
		need += 2 * (gbstrlen(sites) + 1);
	// otherwise, use the site ptrs
	for ( int32_t i = 0 ; i < numSitePtrs ; i++ )
		need += 2 * (gbstrlen(sitePtrs[i]) + 1);

	// how big is each request's header?
	int32_t header = 0;
	// request size
	header += 4;
	// niceness
	header += 1;
	// collection
	header += collLen + 1;
	// flag
	header += 1;
	// the tag rec
	header += tagRec->getSize();
	// . add ST_SITE to each tagRec
	// . we already accounted for the sites in the gbstrlen() above
	header += sizeof(Tag);
	// one header per url
	need += header * numUrls;
	
	// make a request buffer for all the requests
	m_requestBuf = (char *)mmalloc ( need , "msg9a-add");
	if ( ! m_requestBuf ) return true;
	m_requestBufSize = need;

	// carve it up
	char *p = m_requestBuf;
	// loop over sites
	s = sites;
	// reset sitePtr counter in case we are using those
	int32_t si = 0;

	//int32_t now = getTimeGlobal();

	// loop it
	for ( ; ; si++ ) {
		// stop if all done
		if ( sites && ! *s ) break;

		// or this
		if ( sitePtrs && si >= numSitePtrs ) break;
		// make "s" point to the site if we are using ptrs
		if ( sitePtrs ) s = sitePtrs[si];

		// skip whitespace
		while ( *s && is_wspace_a(*s) ) s++;
		// skip over http:// (wastes space)
		if ( strncmp(s,"http://",7)==0 ) s += 7;
		// find end of url
		char *send = s;
		while ( *send && ! is_wspace_a(*send)) send++;
		// get the length
		int32_t len = send - s;
		// done? make sure we are using the site buffer and not ptrs
		if ( sites && ! *s ) break;
		// a place holder for the request size
		int32_t *rsizePtr = (int32_t *)p; p += 4;
		// track the size
		char *start = p;
		// first niceness
		*p = niceness; p++;
		// then coll
		gbmemcpy ( p , coll , collLen ); p += collLen;
		// NULL term
		*p++ = '\0';
		// add flag first
		*p = 0x00;
		//if ( deleteTags  ) *p = 0x01;
		if ( nukeTagRecs ) *p = 0x02; // delete entire TagRec?
		p++;
		// now make the Tag!
		//TagRec *tagRec = (TagRec *)p;
		// sets its ip special if we should
		int32_t ip = 0;
		if ( ipVector ) ip = ipVector[si];
		// . copy it over
		// . get the size
		int32_t size = tagRec->getSize();
		// add in tagRec
		gbmemcpy ( p , tagRec , size );
		// cat it to p
		TagRec *newgr = (TagRec *)p;
		// NULL terminate it temporarily
		char c = s[len];
		s[len] = 0;
		// . remove the old site so the new one can replace it
		// . we already contain a SITE_TAG and addTag() will NEVER
		//   replace that particular tag...
		// . this is now removed above
		//newgr->removeTag ( "site" , NULL );
		// add the site
		//newgr->addTag ( ST_SITE, now,"tagdb",0,s, len+1 );
		newgr->addTag ( "site", timestamp,"tagdb",ip,s, len+1 );
		// undo the NULL termination
		s[len] = c;
		// update the size
		size = newgr->getSize();
		// advance
		p += size;
		// how big was the request, store that
		*rsizePtr = (p - start);
		// advance s
		s = send;
	}


	// reset ptr to request to launch
	m_p = m_requestBuf;
	// sanity check
	if ( p - m_requestBuf > need ) { char *xx=NULL;*xx=0; }
	// all done
	m_pend = p;
	// launch them
	if ( ! launchAddRequests () ) return false;
	// hey that should always block!
	if ( ! g_errno ) { char *xx=NULL; *xx=0; }
	// show erroer
	log("tagdb: msg9a: %s",mstrerror(g_errno));
	// free the allocated mem
	reset();
	// did not block...
	return true;
}

// . "dumpFile" format contains one tag record per line as
//   dumped from './gb dump S main 0 -1 1' cmd line cmd.
// . it is the format given by the TagRec::printToBuf() cmd
bool Msg9a::addTags ( char    *dumpFile               ,
		      char    *coll                   , 
		      void    *state                  ,
		      void   (*callback)(void *state) ,
		      int32_t     niceness               ) {

	g_errno = 0;

	// reset
	m_errno    = 0;
	m_requests = 0;
	m_replies  = 0;
	m_niceness = niceness;
	m_state    = state;
	m_callback = callback;

	int32_t collLen = gbstrlen(coll);
	// scan the dump file
	char *p = dumpFile;
	// the end of it
	char *pend = p + gbstrlen(p);
	// add up total sizes
	int32_t sum = 0;
	// end of line ptr
	char *eol;
	// count
	int32_t count = 1;
	// debug
	//HashTable ht;
	// do the scan
	for ( ; p < pend ; p = eol + 1 ) {
		// point to next line
		eol = p; while ( eol < pend && *eol != '\n' ) eol++;
		// a fake tag rec
		TagRec gr;
		// . scan it into "gr"
		// . returns size of the tag rec stored into "buf"
		int32_t bytesScanned = gr.setFromBuf ( p , eol );
		// error?
		if ( bytesScanned <= 0 ) {count++; continue;}
		// get size
		int32_t size = gr.getSize();
		// error?
		if ( size <= 0 ) {count++; continue;}
		//logf(LOG_DEBUG,"tagdb: tag %"INT32" size=%"INT32"",count++,size);
		// hash it for debug
		//ht.addKey ( count , size );
		count++;
		// sanity check
		if ( size > MAX_TAGREC_SIZE ) { char *xx=NULL;*xx=0;}
		// sanity check
		char *site = gr.getString("site",NULL);
		if ( ! site ) { char *xx=NULL;*xx=0;}
		// then request header size
		size += 4 + 1 + collLen + 1 + 1;
		// increment total size
		sum += size;
	}

	// make the buf
	m_requestBuf = (char *)mmalloc ( sum , "msg9adbuf");
	m_requestBufSize = sum;
	// store tags here
	char *t = m_requestBuf;
	// return true on error with g_errno set
	if ( ! t ) return true;
	// reset to beginning of file
	p = dumpFile;
	// reset
	count = 1;
	// do the scan
	for ( ; p < pend ; p = eol + 1 ) {
		// point to next line
		eol = p; while ( eol < pend && *eol != '\n' ) eol++;
		// first is the request size
		int32_t *requestSizePtr = (int32_t *)t; t += 4;
		// see how big the request is
		char *a = t;
		// then niceness
		*t++ = (char)MAX_NICENESS;
		// then coll
		gbmemcpy ( t , coll , collLen ); t += collLen;
		// null temrinate
		*t++ = '\0';
		// then the 1 byte flag (0 means add?)
		*t++ = 0; 
		// store TagRec into the request buffer
		TagRec *gr = (TagRec *)t;
		// . scan it into "t"
		// . returns size of the tag rec stored into "buf"
		int32_t bytesScanned = gr->setFromBuf ( p , eol );
		// error?
		if ( bytesScanned <= 0 ) {
			log("tagdb: skipping tag rec #%"INT32".",count++);
			t -= (4+1+collLen+1+1);
			continue;
		}
		// get size
		int32_t size = gr->getSize();
		// error?
		if ( size <= 0 ) { 
			log("tagdb: skipping tag rec #%"INT32".",count++);
			t -= (4+1+collLen+1+1);
			continue;
		}
		// test it
		//int32_t slot = ht.getSlot ( count );
		//if ( slot < 0 ) { char *xx=NULL;*xx=0; }
		//int32_t shouldbe = ht.getValueFromSlot ( slot );
		//if ( size != shouldbe ) { char *xx=NULL;*xx=0; }
		count++;
		//logf(LOG_DEBUG,"tagdb: tag %"INT32" size=%"INT32"",count++,size);
		// increment storage ptr
		t += size;
		// store the size of the WHOLE REQUEST, does not
		// include the request size itself. see 
		// launchRequests() below.
		*requestSizePtr = (t - a);
		// sanity check
		if ( *requestSizePtr > 10000 ) { char*xx=NULL;*xx=0;}
	}
	// sanity check
	if ( t - m_requestBuf != sum ) { char *xx=NULL;*xx=0; }
	// use their ptrs for adding these tag recs
	m_p    = m_requestBuf;
	m_pend = m_requestBuf + m_requestBufSize ;
	// now add those tags
	return launchAddRequests ( );
}

// . returns false if blocked, true otherwise
// . sets g_errno and returns true on error
bool Msg9a::launchAddRequests ( ) {
	// clear it
	g_errno = 0;
 loop:
	// return true if nothing to launch
	if ( m_p >= m_pend ) return (m_requests == m_replies);
	// don't bother if already got an error
	if ( m_errno ) return (m_requests == m_replies);
	// limit max oustanding to 20
	if (m_requests - m_replies >= 20 ) return (m_requests==m_replies);
	// take a breath
	QUICKPOLL(m_niceness);

	// parse our request
	char *p = m_p;
	// first is the request size
	p += 4;
	// then niceness
	p += 1;
	// then coll
	p += gbstrlen(p) + 1;
	// then the 1 byte flag
	p++;
	// then the tag rec
	TagRec *tagRec = (TagRec *)p;
	// . get the groupid
	// . tagRec's key should already be valid because when you add
	//   a ST_SITE to a TagRec it sets TagRec::m_key (special thing)
	//uint32_t groupId = g_tagdb.getGroupId ( &tagRec->m_key );
	uint32_t shardNum = getShardNum ( RDB_TAGDB , &tagRec->m_key );
	// get the host to send to
	Host *hosts = g_hostdb.getGroup ( groupId );
	// select a host in the group
	int32_t hostNum = tagRec->m_key.n1 % g_hostdb.getNumHostsPerShard();
	// and his ptr
	Host *h = &hosts[hostNum];

	// get the next mcast
	//Multicast *m = &m_casts[m_requests];
	// reqeust size
	int32_t  requestSize = *(int32_t *)m_p; m_p += 4;
	char *request     =          m_p; m_p += requestSize;
	
	// . send to just one very specific host so he is the only one that
	//   controls modification to this particular tagdb rec. that way if
	//   we are changing its Tags we do not collide with another.
	// . this returns false and sets g_errno on error
	UdpServer *us = &g_udpServer;
	bool status = us->sendRequest ( request           ,
					requestSize       ,
					0x9a              ,
					h->m_ip           , // bestIp 
					h->m_port         , // destPort
					h->m_hostId       , // hostId
					NULL              , // slotPtr
					this              , // state
					gotReplyWrapper9a , // callback
					365*24*3600       , // timeout
					-1                , // backoff
					-1                , // max wait in ms
					NULL              , // replybuf
					0                 , // replybufMaxSize
					m_niceness        );
	// error?
	if ( ! status ) {
		// g_errno should be set, we had an error
		m_errno = g_errno;
		return (m_requests == m_replies);
	}
	// successfully launched
	m_requests++;
	// launch another
	goto loop;
}

void gotReplyWrapper9a ( void *state , UdpSlot *slot ) {
	Msg9a *THIS = (Msg9a *) state;
	THIS->m_replies++;
	// don't let him free our send buf, it is m_requestBuf
	// which we allocated above
	slot->m_sendBufAlloc = NULL;
	// error? if so, save it
	if ( g_errno && ! THIS->m_errno ) THIS->m_errno = g_errno;
	if ( ! THIS->launchAddRequests() ) return;
	// free the allocated mem
	THIS->reset();
	THIS->m_callback ( THIS->m_state );
}

class State9a {
public:
	UdpSlot *m_slot;
	Msg5     m_msg5;
	char     m_requestType;
	Msg1     m_msg1;
	RdbList  m_list;
	// this has all the tags we need to add/remove/replace
	TagRec  *m_tagRec;
	// this has the original tagRec and we modify it with "m_tagRec"
	// to get the final TagRec we add back to Tagdb. it is the
	// "accumulator" tagdb record.
	TagRec   m_accRec;
	// enough mem to store a key_t and a 0 dataSize (int32_t)
	char     m_tmp[12+4];

	char     m_niceness;
	char    *m_coll;

	// linked list of ppl waiting in line to make mods
	class State9a *m_next;
	//class State9a *m_tail;
};

void handleRequest9a ( UdpSlot *slot , int32_t niceness ) {
	// get the request
	char *request     = slot->m_readBuf;
	int32_t  requestSize = slot->m_readBufSize;
	// overflow protection for corrupt requests
	if ( requestSize < 4 ) {
		g_errno = EBUFTOOSMALL;
		g_udpServer.sendErrorReply ( slot , g_errno );
		return;
	}
	// make a new Msg9a
	State9a *st ;
	try { st = new (State9a); }
	catch ( ... ) {
		g_errno = ENOMEM;
		log("msg9a: new(%i): %s", sizeof(State9a), mstrerror(g_errno));
		return g_udpServer.sendErrorReply ( slot, g_errno );
	}
	mnew ( st , sizeof(State9a) , "Msg10" );

	// parse the request
	char *p = request;
	// save slot for sending reply
	st->m_slot = slot;
	// get niceness
	st->m_niceness = *(char *)p; p++;
	// get coll
	st->m_coll = p; p += gbstrlen(p) + 1;
	// save this
	st->m_requestType = *p; p++;
	// the "tagRec" is the record
	TagRec *tagRec = (TagRec *)p; p += tagRec->getSize();
	// store ptr
	st->m_tagRec = tagRec;
	// reset this, we are the head/tail of the linked list so far
	st->m_next = NULL;

	// sanity check
	//char *site = tagRec->getString(ST_SITE,NULL);
	char *site = tagRec->getString("site",NULL);
	// this is a no-no
	if ( ! site ) { char *xx=NULL;*xx=0;}

	// no tail after us
	//st->m_tail = NULL;

	// . get the lock on this site
	// . the lower 64 bits of the key should be the url hash
	int32_t slotNum = s_lockTable2.getSlot ( &st->m_tagRec->m_key.n0 );
	// if already in there, we have to wait because someone is already
	// making mods to this TagRec
	if ( slotNum >= 0 ) {
		// log this for now?
		if ( g_conf.m_logDebugSpider )
			logf(LOG_DEBUG,"tagdb: TAGDB handleRequest9a "
			     "waiting for lock st=0x%"XINT32" key.n0=%"UINT64"",(int32_t)st,
			     st->m_tagRec->m_key.n0);
		State9a *p ;
		p = *(State9a **)s_lockTable2.getValueFromSlot(slotNum);
		// put us right after him in the linked list
		st->m_next = p->m_next;
		p->m_next  = st;
		// we could be the next in line
		//if ( ! p->m_next ) p->m_next = st;
		// we wait...
		return;
	}

	// delete our slot from the lock table
	if ( ! s_lockTable2.addKey ( &st->m_tagRec->m_key.n0 , &st ) ) {
		log("tagdb: failed to get lock : %s",mstrerror(g_errno));
		// free him, we sent his reply
		mdelete ( st , sizeof(State9a),"msg9afr");
		delete (st);
		return g_udpServer.sendErrorReply ( slot, g_errno );
	}

	// make a startKey and endKey from the tagRec's key
	key_t startKey = tagRec->m_key;
	key_t endKey   = tagRec->m_key;
	// startkey gets is low bit cleared though
	startKey.n0 &= 0xfffffffffffffffeLL;	

	// delete record request, no need to look it up
	if ( st->m_requestType == 0x02 ) {
		// note it
		SafeBuf sb; tagRec->printToBuf ( &sb );
		log("tagdb: deleting TagRec for site %s",sb.getBufStart());
		// use tmp buf in st
		char *p = st->m_tmp;
		// store key in the tmp buf
		*(key_t *)p = startKey;
		// advance
		p += sizeof(key_t);
		// and store the data size
		*(int32_t *)p = 0;
		// advance
		p += 4;
		// set the list (just a negative rec in it)
		st->m_list.set ( st->m_tmp         , // list
				 4+sizeof(key_t)   , // listSize
				 st->m_tmp         , // alloc
				 4+sizeof(key_t)   , // allocSize
				 (char *)&startKey , // startKey
				 (char *)&endKey   , // endKey
				 -1                , // fixeDataSize
				 false             , // ownData?
				 false             , // useHalfKeys?
				 sizeof(key_t)     );// keySize

		if ( ! st->m_msg1.addList( &st->m_list    ,
					   RDB_TAGDB      ,
					   st->m_coll     ,
					   st             ,
					   sendReply9a    ,
					   false          , // forceLocal?
					   st->m_niceness ))
			// return if blocked
			return;
		sendReply9a( st );
		return;
	}

	// . get from msg5, return if it blocked
	// . will probably not block since in the disk page cache a lot
	if ( ! st->m_msg5.getList ( RDB_TAGDB      ,
				    st->m_coll     ,
				    &st->m_list    ,
				    startKey       ,
				    endKey         ,
				    100000         , // minRecSizes
				    true           , // include tree?
				    false          , // addtocache?
				    0              , // maxcacheage
				    0              , // startfilenum
				    -1             , // numFiles
				    st             ,
				    gotList        ,
				    st->m_niceness ,
				    true           ))// do err correction?
		return;
	// log that for debug
	//log("tagdb: msg5 call did not block. st=%"UINT32"",(int32_t)st);
	// sanity check - why not block if it had corruption?
	if ( st->m_msg5.m_msg3.m_hadCorruption ) { char *xx=NULL;*xx=0; }
	// it did not block...
	gotList( st , NULL , NULL );
}

void gotList ( void *state , RdbList *xxx , Msg5 *yyy ) {
	// cast our state class
	State9a *st = (State9a *)state;
	// return right away if error getting the rec
	if ( g_errno ) { sendReply9a ( st ); return; }
	// note it
	//log("tagdb: in gotlist st=%"UINT32"",(int32_t)st);
	// this is the TagRec rdb record
	char *rec     = st->m_list.getList    ();
	int32_t  recSize = st->m_list.getListSize();
	// cast it as a TagRec
	TagRec *accRec = &st->m_accRec;
	// reset in case not in tagdb and rec/recSize is NULL/0
	accRec->reset();
	// copy it to our accumulator rec which has room to grow, the list
	// does not
	gbmemcpy ( (char *)accRec , rec , recSize );
	// free that list buffer now, we copied it into a larger buffer
	st->m_list.reset();

 loop:
	// clear it
	g_errno = 0;
	// . add/remove the tags from the tagRec
	// . add will replace tags with the same tag id and username
	// . should deal with "negative" tags (addDelTag())
	//if ( st->m_requestType == 0x00 ) accRec->addTags    ( st->m_tagRec );
	//else                             accRec->removeTags ( st->m_tagRec );
	accRec->addTags ( st->m_tagRec );
	// was there an error? abandon all operations on this TagRec if so
	if ( g_errno ) { sendReply9a ( st ); return; }
	// perform operations on others in the queue
	st = st->m_next;
	// debug for now
	if ( st && g_conf.m_logDebugSpider ) 
		logf(LOG_DEBUG,"tagdb: calling lock for st=0x%"XINT32"",(int32_t)st);
	// if there was one, do it
	if ( st ) goto loop;
	// reset to original parent
	st = (State9a *)state;
	// debug msg
	SafeBuf sb; accRec->printToBuf ( &sb );
	log(LOG_DEBUG,"tagdb: adding to tagdb: %s",sb.getBufStart());

	// set the list, it should free itself
	st->m_list.set ( (char *)accRec    , // list
			 accRec->getSize() , // allocSize
			 (char *)accRec    , // alloc
			 accRec->getSize() , // allocSize
			 (char *)&accRec->m_key , // startKey
			 (char *)&accRec->m_key , // endKey
			 -1                , // fixeDataSize
			 false             , // ownData?
			 false             , // useHalfKeys?
			 sizeof(key_t)     );// keySize

	// add it back after the mods
	if ( ! st->m_msg1.addList( &st->m_list    ,
				   RDB_TAGDB      ,
				   st->m_coll     ,
				   st             ,
				   sendReply9a    ,
				   false          , // forceLocal?
				   MAX_NICENESS   ))// niceness
		return;
	// i giess we did not block! send back the reply...
	sendReply9a ( st );
}

void sendReply9a ( void *state ) {
	// cast our state class
	State9a *st = (State9a *)state;
	// delete our slot from the lock table
	s_lockTable2.removeKey ( &st->m_tagRec->m_key.n0 );
	// log it
	if (g_errno) log("tagdb: msg9a failed to add: %s",mstrerror(g_errno));
	// save it, in case a function below clears g_errno
	int32_t saved = g_errno;

 loop:
	if ( saved ) g_udpServer.sendErrorReply( st->m_slot,saved);
	// send empty reply
	else         g_udpServer.sendReply_ass(NULL,0,NULL,0,st->m_slot);
	// save old guy
	State9a *next = st->m_next;
	// free him, we sent his reply
	mdelete ( st , sizeof(State9a),"msg9afr");
	delete (st);
	// repeat for each guy waiting in line
	st = next;
	// if there was one, do it
	if ( st ) goto loop;
	// reset to original parent
	st = (State9a *)state;
}
*/

///////////////////////////////////////////////
//
// OTHER functions
//
///////////////////////////////////////////////

int32_t getY ( int64_t X , int64_t *x , int64_t *y , int32_t n ) {
	// if we only have one point then there'll be no interpolation
	if ( n == 1 ) return y[0];
	// find the first x after our "X"
	int32_t j;
	for ( j = 0 ; j < n; j++ ) if ( x[j] >= X ) break;
	// before/after first/last point means we don't have to interpolate
	if ( j <= 0 ) return y[0  ];
	if ( j >= n ) return y[n-1];
	// linear interpolate between our 2 points (x0,y0) and (x1,y1)
	int64_t x0 = x[j-1];
	int64_t x1 = x[j  ];
	int64_t y0 = y[j-1];
	int64_t y1 = y[j  ];
	// error if x1 less than x0
	if ( x1 <= x0 ) {
		log("tagdb: X coordinates are not in ascending order for map");
		char *xx=NULL;*xx=0;
	}
	// otherwise we have a sloping line
	return  y0 + ( ((int64_t)X - x0) * (y1-y0) ) /(x1-x0) ;
}

///////////////////////////////////////////////
//
// sendPageTagdb() is the HTML interface to tagdb
//
///////////////////////////////////////////////

static void sendReplyWrapper  ( void *state ) ;
static void sendReplyWrapper2 ( void *state ) ;
static bool sendReply         ( void *state ) ;
static bool sendReply2        ( void *state ) ;
static bool getTagRec ( class State12 *st );

// don't change name to "State" cuz that might conflict with another
class State12 {
public:
	//Msg9a        m_msg9a;
	TcpSocket   *m_socket;
	bool         m_adding;
	//char        *m_coll;
	collnum_t m_collnum;
	//int32_t         m_collLen;
	//char        *m_buf;
	//int32_t         m_bufLen;
	bool         m_isLocal;
	//int32_t         m_fileNum;
	//bool         m_isMasterAdmin;
	//bool         m_isAssassin;
	// . Commented by Gourav
	// .  Reason:user perm no longer used
	//char         m_userType;
	HttpRequest  m_r;
	//char        *m_username;
	TagRec       m_tagRec;
	TagRec       m_newtr;
	Msg8a        m_msg8a;
	Url          m_url;
	char        *m_urls;
	int32_t         m_urlsLen;
	Msg1         m_msg1;
	RdbList      m_list;
	//Msg1         m_msg1;
	int32_t         m_niceness;
	bool         m_mergeTags;
	//char         m_tmp[16];
};

// . returns false if blocked, true otherwise
// . sets g_errno on error
// . make a web page displaying the tagdb interface
// . call g_httpServer.sendDynamicPage() to send it
// . show a textarea for sites, then list all the different site tags
//   and have an option to add/delete them
bool sendPageTagdb ( TcpSocket *s , HttpRequest *req ) {
	// are we the admin?
	//bool isAdmin    = g_collectiondb.isAdmin    ( req , s );
	// get the collection record
	CollectionRec *cr = g_collectiondb.getRec ( req );
	if ( ! cr ) {
		g_errno = ENOCOLLREC;
		log("admin: No collection record found "
		    "for specified collection name. Could not add sites to "
		    "tagdb. Returning HTTP status of 500.");
		return g_httpServer.sendErrorReply ( s , 500 ,
						  "collection does not exist");
	}
	/*
	bool isAssassin = cr->isAssassin ( s->m_ip );
	if ( isAdmin ) isAssassin = true;
	// bail if permission denied
	if ( ! isAssassin ){ 
	//&& ! cr->hasPermission ( req , s ) ) {
		log("admin: Bad collection name or password. Could not add "
		    "sites to tagdb. Permission denied.");
		return sendPagexxxx( s , req , 
						    "Collection name or "
						    "password is incorrect");
	}
	*/
	// make a state
	State12 *st ;
	try { st = new (State12); }
	catch ( ... ) {
		g_errno = ENOMEM;
		log("PageTagdb: new(%"INT32"): %s", 
		    (int32_t)sizeof(State12),mstrerror(g_errno));
		return g_httpServer.sendErrorReply(s,500,mstrerror(g_errno));}
	mnew ( st , sizeof(State12) , "PageTagdb" );
	//st->m_isMasterAdmin    = isAdmin;
	//st->m_isAssassin = isAssassin;
	// . Commented by Gourav
	// .  Reason:user perm no longer used
	//st->m_userType   = g_pages.getUserType ( s , req );
	// assume we've nothing to add
	st->m_adding = false;
	// save the socket
	st->m_socket = s;
	// i guess this is nuked, so copy it
	st->m_r.copy ( req );
	// make it high priority
	st->m_niceness = 0;
	// point to it
	HttpRequest *r = &st->m_r;

	// get the collection
	int32_t  collLen = 0;
	char *coll  = r->getString ( "c" , &collLen  , NULL /*default*/);
	// get collection rec
	CollectionRec *cr2 = g_collectiondb.getRec ( coll );
	// bitch if no collection rec found
	if ( ! cr2 || ! coll || collLen+1 > MAX_COLL_LEN ) {
		g_errno = ENOCOLLREC;
		log("admin: No collection record found "
		    "for specified collection name. Could not add sites to "
		    "tagdb. Returning HTTP status of 500.");
		mdelete ( st , sizeof(State12) , "PageTagdb" );
		delete (st);
		return g_httpServer.sendErrorReply ( s , 500 ,
						  "collection does not exist");
	}

	// . get fields from cgi field of the requested url
	// . get the null-terminated, space-separated lists of sites to add
	int32_t  urlsLen = 0;
	char *urls = r->getString ( "u" , &urlsLen , NULL /*default*/);
	
	//a quick hack so we can put multiple sites in a link
	if(r->getLong("uenc", 0)) 
		for(int32_t i = 0; i < urlsLen; i++) 
			if(urls[i] == '+') urls[i] = '\n';
	// get the file # of the tagdb file these sites should use
	//int32_t  fileNum = r->getLong ("f",-1);
	// get the archive filename of sites to add
	/*
	int32_t  xlen;
	char *x = r->getString("x",&xlen,NULL);
	// trim off any spaces
	while ( xlen > 0 && is_wspace_a(x[xlen-1]) ) x[--xlen]='\0';
	*/
	// . get the username
	// . just get from cookie so it is not broadcast over the web via a 
	//   referral url
	//st->m_username = r->getStringFromCookie("username");
	//st->m_username = g_users.getUsername(r);

	// are we coming from a local machine?
	st->m_isLocal = r->isLocal();
	/*
	// don't set this unless we have to free it
	st->m_buf    = NULL;
	st->m_bufLen = 0;
	// . set our archive filename of sites to add with this fileNum
	// . "a" will be NULL if none supplied
	if ( xlen ) {
		File file;
		file.set ( x );
		// add 1 to bufLen for terminating \0
		int32_t  bufLen = file.getFileSize() + 1 ;
		char *buf    = (char *) mmalloc ( bufLen , "PageTagdb");
		if ( ! buf ) {
			log("admin: File of sites is too big to add to tagdb."
			    " Allocation of %"INT32" bytes failed.",bufLen);
			mdelete ( st , sizeof(State12) , "PageTagdb" );
			delete (st);
			return g_httpServer.sendErrorReply(s,500,
							   mstrerror(g_errno));
		}
		file.open(O_RDONLY);
		file.read ( buf , bufLen - 1 , 0 );
		// NULL terminate the list of urls
		buf [ bufLen - 1 ] = '\0';
		st->m_buf    = buf;
		st->m_bufLen = bufLen ;
		urls    = buf;
		urlsLen = bufLen;
	}		
	*/
	// it references into the request, should be ok
	//st->m_coll    = coll;
	st->m_collnum = cr->m_collnum;
	//st->m_collLen = collLen;
	//strcpy ( st->m_coll , coll );
	// do not print "(null)" in the textarea
	if ( ! urls ) urls = "";

	// the url buffer
	st->m_urls    = urls;
	st->m_urlsLen = urlsLen;

	// sanity check
	//bool  delOp = r->getLong   ("delop",0    );
	//char *nuke  = r->getString ("nuke" ,NULL );
	//if ( nuke && ! delOp ) {
	//	g_errno = EBADENGINEER;
	//	log("tagdb: delete operation checkbox not checked.");
	//	mdelete ( st , sizeof(State12) , "PageTagdb" );
	//	delete (st);
	//	return g_httpServer.sendErrorReply(s,500,
	//					   mstrerror(g_errno));
	//}

	int32_t ufuLen;
	char *ufu = r->getString("ufu",&ufuLen);

	if ( urls[0] == '\0' && ! ufu ) return sendReply ( st );

	char *get = r->getString ("get",NULL );
	// this is also a get operation but merges the tags from all TagRecs
	char *merge = r->getString("tags",NULL);

	// is this an add/update operation? or just get?
	if ( get || merge ) st->m_adding = false;
	else                st->m_adding = true;


	// if each line in the file is the output of a tagdb dump
	// operation on the cmd line like this:
	// k.n1=0x892f9 k.n0=0xac2ff39f8112b71f version=0 TAG=ruleset,
	// "mwells",1,Jan-02-2009-18:26:04,333333333,67.16.94.2,3735437892,36 
	// THEN we should just call msg9a directly and it should create
	// a tag rec for each line and add that
	/*
	bool isDumpFile = false;
	if ( urls && strncmp(urls,"k.n1=",5)==0 ) isDumpFile = true;
	if ( isDumpFile ) {
		if ( ! st->m_msg9a.addTags ( st->m_urls        , // dumpFile
					     st->m_coll        ,
					     st                ,
					     sendReplyWrapper2 ,
					     0                 ))// niceness
			return false;
		return sendReply2 ( st );
	}
	*/

	// get/merge operations can skip the tag rec lookup
	//if ( ! st->m_adding ) return sendReply ( st );

	// regardless, we have to get the tagrec for all operations
	//Url site;
	//site.set(urls,gbstrlen(urls));
	st->m_url.set(urls,gbstrlen(urls));
	st->m_mergeTags = merge;

	return getTagRec ( st );
}

bool getTagRec ( State12 *st ) {

	bool doInheritance = st->m_mergeTags;//(bool)merge;
	char rdbId = RDB_TAGDB;
	// fbid09729034234.com then use facebookdb
	//char *host = site.getHost();
	//if ( strncmp(host,"fbid",4)==0 &&  is_digit(host[4]) )
	//	rdbId = RDB_FACEBOOKDB;
	// this replaces msg8a
	if ( ! st->m_msg8a.getTagRec ( &st->m_url,//&site , 
				       // tell msg8a to try to guess the site
				       NULL,
				       st->m_collnum ,
				       false, // skip dom lookup?
				       st->m_niceness ,
				       st ,
				       sendReplyWrapper ,
				       &st->m_tagRec ,
				       doInheritance ,
				       rdbId))
		return false;
	/*
	if ( ! st->m_msg8a.getTagRec ( &site , // &st->m_url,
				       st->m_coll,
				       st->m_collLen,
				       true, //usecanonicalName
				       0, //niceness
				       st,
				       sendReplyWrapper ,
				       &st->m_tagRec ,
				       doInheritance )){
		return false;
	}
	*/
	return sendReply ( st );
}

void sendReplyWrapper ( void *state ) {
	sendReply ( state );
}

static void sendReplyWrapper2 ( void *state ) {
	State12 *st = (State12 *)state;
	// re-get the tags from msg8a since we changed them
	getTagRec(st);
	//sendReply2 ( state );
}

bool sendReply ( void *state ) {

	// get our state class
	State12 *st = (State12 *) state;
	// get the request
	HttpRequest *r = &st->m_r;
	// and socket
	TcpSocket *s = st->m_socket;
	// the tagrec
	//TagRec *gr = &st->m_tagRec;
	// reset "gr" so it won't show the old tags of the first rec
	// in the text area box on the tagdb page after the add is completed
	//if ( st->m_adding ) gr->reset();

	// . if urlsLen <= 0 or fileNum < 0 and we're not deleting 
	// . then we've nothing to add
	//if ( urlsLen <= 0 ) return sendReply ( st );

	// need a valid username
	//if ( ! st->m_username || st->m_username[0] == '\0' ) {
	//	log("tagdb: bad username.");
	//	mdelete ( st , sizeof(State12) , "PageTagdb" );
	//	delete (st);
	//	return g_httpServer.sendErrorReply(s,500,
	//					   mstrerror(g_errno));
	//}

	if ( ! st->m_adding ) return sendReply2 ( st );

	// no permmission?
	bool isMasterAdmin = g_conf.isMasterAdmin ( s , r );
	bool isCollAdmin = g_conf.isCollAdmin ( s , r );
	if ( ! isMasterAdmin &&
	     ! isCollAdmin ) {
		g_errno = ENOPERM;
		return sendReply2 ( st );
	}

	//char *nuke = r->getString ("nuke" ,NULL );

	TagRec *newtr = &st->m_newtr;
	// update it from the http request
	newtr->setFromHttpRequest ( r , s );
	// but remove the site tag
	//newtr.removeTags ( "site" , NULL );
	// add it into gr
	//gr->addTags ( &newtr );
	// copy it over to our state
	//gbmemcpy ( gr , &newtr , newtr.getSize() );

	// debug
	// this doesn't work because we do not set TagRec::m_listPtrs[0]
	// to point to the list we make below (MDW 4/29/13)
	//SafeBuf tmp;
	//newtr->printToBuf ( &tmp );
	//log(LOG_DEBUG,"tagdb: converted from http: %s", 
	//    tmp.getBufStart() );

	// make a startKey and endKey from the tagRec's key
	//key_t startKey = gr->m_key;
	//key_t endKey   = gr->m_key;
	// startkey gets is low bit cleared though
	//startKey.n0 &= 0xfffffffffffffffeLL;	

	/*
	// add using msg9a
	if ( ! st->m_msg9a.addTags ( st->m_urls        ,
				     NULL              , // sitePtrs
				     0                 , // numSitePtrs
				     st->m_coll        ,
				     st                ,
				     sendReplyWrapper2 ,
				     0                 , // niceness
				     &newtr            , // gr
				     nuke              ,
				     NULL              )) // ipvec
		return false;
	*/

	// shrotcut
	SafeBuf *sbuf = &newtr->m_sbuf;
	// use the list we got
	RdbList *list = &st->m_list;
	key128_t startKey;
	key128_t endKey;
	startKey.setMin();
	endKey.setMax();
	// set it from safe buf
	list->set ( sbuf->getBufStart() ,
		    sbuf->length() ,
		    NULL ,
		    0 ,
		    (char *)&startKey ,
		    (char *)&endKey  ,
		    -1 ,
		    false ,
		    false ,
		    sizeof(key128_t) );

	// no longer adding
	st->m_adding = false;

	// . just use TagRec::m_msg1 now
	// . no, can't use that because tags are added using SafeBuf::addTag()
	//   which first pushes the rdbid, so we gotta use msg4
	if ( ! st->m_msg1.addList ( list ,
				    RDB_TAGDB ,
				    st->m_collnum ,
				    st ,
				    sendReplyWrapper2 ,
				    false ,
				    st->m_niceness ) )
		return false;

        // . if addTagRecs() doesn't block then sendReply right away
        // . this returns false if blocks, true otherwise
        //return sendReply2 ( st );
	return getTagRec ( st );
}

bool sendReply2 ( void *state ) {

	// get our state class
	State12 *st = (State12 *) state;
	// get the request
	HttpRequest *r = &st->m_r;
	// and socket
	TcpSocket *s = st->m_socket;

	// page is not more than 32k
	char buf[1024*32];
	SafeBuf sb(buf, 1024*32);
	// do they want an xml reply?
	if( r->getLong("xml",0) ) { // was "raw"
		sb.safePrintf("<?xml version=\"1.0\" "
			      "encoding=\"ISO-8859-1\"?>\n"
			      "<response>\n");
	
		st->m_tagRec.printToBufAsXml(&sb);
		
		sb.safePrintf("</response>");
		log ( LOG_INFO,"sending raw page###\n");
		// clear g_errno, if any, so our reply send goes through
		g_errno = 0;
		// extract the socket
		TcpSocket *s = st->m_socket;
		// . nuke the state
		// . first free the buffer, if non-NULL
		//if (st->m_buf) mfree (st->m_buf, st->m_bufLen, "PageTagdb");
		mdelete(st, sizeof(State12), "PageTagdb");
		delete (st);
		// . send this page
		// . encapsulates in html header and tail
		// . make a Mime
		return g_httpServer.sendDynamicPage(s, sb.getBufStart(), 
                                                    sb.length(),
						    0, false, "text/xml",
						    -1, NULL, "ISO-8859-1");
	}
	// . print standard header
	// . do not print big links if only an assassin, just print host ids
	g_pages.printAdminTop ( &sb, st->m_socket , &st->m_r );
	// did we add some sites???
	if ( st->m_adding ) {
		// if there was an error let them know
		if ( g_errno )
			sb.safePrintf("<center>Error adding site(s): <b>"
				      "%s[%i]</b><br><br></center>\n",
				      mstrerror(g_errno) , g_errno );
		else   sb.safePrintf ("<center><b><font color=red>"
				      "Sites added successfully"
				      "</font></b><br><br></center>\n");
	}

	//char *c = st->m_coll;
	char bb [ MAX_COLL_LEN + 60 ];
	bb[0]='\0';

	sb.safePrintf(
		      "<style>"
		      ".poo { background-color:#%s;}\n"
		      "</style>\n" ,
		      LIGHT_BLUE );

	// print interface to add sites
	sb.safePrintf (
		  "<table %s>"
		  "<tr><td colspan=2>"
		  "<center><b>Tagdb</b>%s</center>"
		  "</td></tr>", TABLE_STYLE , bb );

	// sometimes we add a huge # of urls, so don't display them because
	// it like freezes the silly browser
	char *uu = st->m_urls;
	if ( st->m_urlsLen > 100000 ) uu = "";
	

	//sb.safePrintf ( "<tr bgcolor=#%s><td colspan=2>"
	//		"<center>"
	//		"</center>"
	//		"</td></tr>",
	//		DARK_BLUE);
	

	sb.safePrintf ( "<tr class=poo><td>"
			"<b>urls</b>"
			"<br>"

			"<font size=-2>"
			"Enter a single URL and then click <i>Get Tags</i> to "
			"get back its tags. Enter multiple URLs and select "
			"the tags names and values in the other table "
			"below in order to tag "
			"them all with those tags when you click "
			"<i>Add Tags</i>. "
			"On the command line you can also issue a "
			"<i>./gb 0 dump S main 0 -1 1</i>"
			"command, for instance, to dump out the tagdb "
			"contents for the <i>main</i> collection on "
			"<i>host #0</i>. "
			"</font>"


			"</td>");

	// text area for adding space separated sites/urls
	//char *pp = "put sites here";
	//char *pp = "";
	//if ( st->m_bufLen > 0 ) pp = st->m_buf; // no, print out "urls"
	sb.safePrintf (""
		       "<td width=70%%>"
		       "<br>"
		       "<textarea rows=16 cols=64 name=u>"
		       "%s</textarea></td></tr>" , uu );

	// spam assassins should not use this much power, too risky
	//if ( st->m_isMasterAdmin ) {
	//	sb.safePrintf ("<i><font size=-1>Note: use 1.2.3.<b>0</b> to "
	//		       "specify ip domain.</i><br>");
	//}

	// allow filename to load them from
	//if ( st->m_isMasterAdmin ) {
	sb.safePrintf("<tr class=poo>"
		      "<td>"
		      "<b>file of urls to tag</b>"
		      "<br>"
		      "<font size=-2>"
		      "If provided, Gigablast will read the URLs from "
		      "this file as if you pasted them into the text "
		      "area above. The text area will also be ignored."
		      "</font>"
		      "</td>"
		      "<td><input name=ufu "
		      "type=text size=40>"//<br>"
		      //"<i>file can also be dumped output of "
		      //"tagdb from the <b>gb dump S ...</b> "
		      //"command.</i>"
		      //"<br><br>" );
		      "</td></tr>"
		      );
	//}

	// this is applied to every tag that is added for accountability
	sb.safePrintf("<tr class=poo><td>"
		      "<b>username</b>"
		      "<br><font size=-2>"
		      "Stored with each tag you add for accountability."
		      "</font>"
		      "</td><td>"
		      "<input name=username type=text size=6 "
		      "value=\"admin\"> " 
		      "</td></tr>"
		      );//,st->m_username);

	// as a safety, this must be checked for any delete operation
	sb.safePrintf ("<tr class=poo><td><b>delete operation</b>"
		       "<br>"
		       "<font size=-2>"

			"If checked "
			"then the tag names you specify below will be "
			"deleted for the URLs you provide in the text area "
			"when you click <i>Add Tags</i>."
		       "</font>"


		       "</td><td><input type=\"checkbox\" "
		       "value=\"1\" name=\"delop\"></td></tr>");

	// close up
	sb.safePrintf ("<tr bgcolor=#%s><td colspan=2>"
		       "<center>"
		       // this is merge all by default right now but since
		       // zak is really only using eventtaghashxxxx.com we
		       // should be ok
		       "<input type=submit name=get "
		       "value=\"Get Tags\" border=0>"

		       //"<input type=submit name=get "
		       //"value=\"get best rec\" border=0>"

		       //"<input type=submit name=tags "
		       //"value=\"merge all matching recs\" border=0>"

		       //"<input type=submit name=nuke "
		       //"value=\"delete recs\" border=0>"

		       //		  "</form>"
		       "</center>"
		       "</td></tr></table>"
		       "<br><br>"
		       , DARK_BLUE
		       );


	// . show all tags we got values for
	// . put a delete checkbox next to each one
	// . show 5-10 dropdowns for adding new tags

	// for some reason the "selected" option tags do not show up below
	// on firefox unless i have this line.

	sb.safePrintf (
		       "<table %s>"
		       "<tr><td colspan=20>"
		       "<center><b>Add Tag</b></center>"
		       "</td></tr>", TABLE_STYLE );


	// count how many "tagRecs" we are taking tags from
	Tag *jtag  = st->m_tagRec.getFirstTag();
	int32_t numTagRecs = 0;
	for ( ; jtag ; jtag = st->m_tagRec.getNextTag(jtag) ) {
		// skip dups
		if ( jtag->m_type == TT_DUP ) continue;
		// count # of TagRecs contributing to the tags
		//if ( tag && tag->m_type == ST_SITE ) numTagRecs++;
		if ( jtag && jtag->isType("site") ) numTagRecs++;
	}

	// if we are displaying a COMBINATION of TagRecs merged together in 
	// the inheritance loop (above) then you can not edit that! you can
	// only edit individual tag recs
	bool canEdit = (numTagRecs <= 1);

	if ( ! canEdit )
		sb.safePrintf("<tr class=poo>"
			      "<td colspan=10><center><font color=red>"
			      "<b>Can not edit because more than one "
			      "TagRecs were merged</b></font></center>"
			      "</td></tr>\n" );

	// headers
	sb.safePrintf("<tr bgcolor=#%s>"
		      //"<td><b>delete?</b></td>"
		      "<td><b>del?</b></td>"
		      "<td><b>tag name</b></td>"
		      "<td><b>tag value</b></td>"
		      "<td><b>datasize (with NULL)</b></td>"
		      "<td><b>username</b></td>"
		      "<td><b>timestamp</b></td>"
		      "<td><b>user ip</b></td>"
		      "<td><b>deduphash32</b></td>"
		      "<td><b>sitehash32</b></td>"
		      "</tr>\n",
		      DARK_BLUE);

	// set up the loop
	Tag *itag  = st->m_tagRec.getFirstTag();
	//last = NULL;
	int32_t count = 0;
	int32_t empty = 0;
	// loop over all tags in TagRec
	for ( ; empty < 3 ; count++ ) {
		// use this tag to print from
		Tag *ctag = itag;
		// advance
		if ( itag ) itag = st->m_tagRec.getNextTag(itag);
		// make it NULL, do not start over at the beginning
		if ( empty > 0 ) ctag = NULL;
		// skip dups
		if ( ctag && ctag->m_type == TT_DUP ) continue;
		// if ctag NULL and we are getting all tags, break
		if ( ! canEdit && ! ctag ) break;
		// assign for looping
		//last = tag;
		// if we are NULL, print out 3 empty tags
		if ( ! ctag ) empty++;
		// start the section
		sb.safePrintf("<tr class=poo>");
		// the delete tag checkbox
		//sb.safePrintf("<tr bgcolor=#%s><td>",DARK_BLUE);
		sb.safePrintf("<td>");
		if ( ctag && canEdit ) // && tag->m_type != ST_SITE ) 
			sb.safePrintf("<input name=deltag%"INT32" "
				      "type=checkbox>",count);
		else     
			sb.safePrintf("&nbsp;");
		sb.safePrintf("</td>");
		// start the next cell
		sb.safePrintf("<td>");
		// . skip ST_SITE, do not show dropdown for that
		// . no, because for looking up tagRecs i like to see
		//   the site tag value, to see what subdomain is matched
		//if ( ctag && ctag->m_type == ST_SITE ) continue;
		// print drop down
		if ( ! ctag ) sb.safePrintf("<select name=tagtype%"INT32">",count);
		// how many tags do we have?
		int32_t n = (int32_t)sizeof(s_tagDesc)/(int32_t)sizeof(TagDesc);
		// the options
		for ( int32_t i = 0 ; ! ctag && i < n ; i++ ) {
			TagDesc *td = &s_tagDesc[i];
			// get tag name
			char *tagName = td->m_name;
			// skip if a reserved tag
			//if ( strncasecmp ( tagName , "reserved" ,8)==0 ) 
			//	continue;
			// select the item in the dropdown
			char *selected = "";
			// was it selected?
			if ( ctag && td->m_type == ctag->m_type ) 
				selected = " selected";
			// show it in the drop down list
			sb.safePrintf("<option value=\"%s\"%s>%s",
				      tagName,selected,tagName);
		}
		// close up the drop down list
		if ( ! ctag ) sb.safePrintf("</select>");
		else {
			char *tagName = getTagStrFromType ( ctag->m_type );
			sb.safePrintf("<input type=hidden name=tagtype%"INT32" "
				      "value=\"%s\">%s",
				      count,tagName,tagName);
		}
		sb.safePrintf("</td><td>");
		// the score field for the drop down list, whatever tag id
		// was selected will have this score
		if ( canEdit )
			sb.safePrintf("<input type=text name=tagdata%"INT32" "
				      "size=50 value=\"",count);
		// show the value
		if ( ctag ) ctag->printDataToBuf ( &sb );
		// close up the input tag
		if ( canEdit ) sb.safePrintf("\">");
		// close up table cell
		sb.safePrintf("\n</td>");

		// if no tag, just placeholders
		if ( ! ctag ) {
			sb.safePrintf("<td>&nbsp;</td>"
				      "<td>&nbsp;</td>"
				      "<td>&nbsp;</td>"
				      "<td>&nbsp;</td>"
				      "<td>&nbsp;</td>"
				      "<td>&nbsp;</td></tr>");
			continue;
		}
		// data size
		sb.safePrintf("<td>%"INT32"</td>",(int32_t)ctag->getTagDataSize());
		// username, timestamp only for non-empty tags
		char *username = ctag->getUser();
		int32_t timestamp = ctag->m_timestamp;
		int32_t  ip  = 0;
		char *ips = "&nbsp;";
		if ( ctag->m_ip ) { ip=ctag->m_ip; ips=iptoa(ctag->m_ip);}
		// convert timestamp to string
		char tmp[64];
		sprintf(tmp,"&nbsp;");
		time_t ts = timestamp;
		struct tm *timeStruct = localtime ( &ts );
		if ( timestamp ) 
			strftime(tmp,64,"%b-%d-%Y-%H:%M:%S",timeStruct);
		sb.safePrintf("<td><input type=hidden name=taguser%"INT32" "
			      "value=%s>%s</td>",
			      count,username,username);
		sb.safePrintf("<td><input type=hidden name=tagtime%"INT32" "
			      "value=%"INT32">%s</td>",
			      count,timestamp,tmp);

		sb.safePrintf("<td><input type=hidden name=tagip%"INT32" "
			      "value=%"INT32">%s",
			      count,ip,ips);

		sb.safePrintf("<input type=hidden name=tagn1key%"INT32" "
			      "value=%"UINT64">",
			      count,ctag->m_key.n1);
		sb.safePrintf("<input type=hidden name=tagn0key%"INT32" "
			      "value=%"UINT64">",
			      count,ctag->m_key.n0);

		sb.safePrintf("</td>");

		sb.safePrintf("<td>0x%"XINT32"</td>", (int32_t)(ctag->m_key.n0>>32) );

		sb.safePrintf("<td>0x%"XINT32"</td>", 
			      // order 1 in since we always do that because
			      // we forgot to shift up one for the delbit
			      // above in Tag::set() when it sets m_key.n0
			      (int32_t)(ctag->m_key.n0&0xffffffff) | 0x01);

		//sb.safePrintf("<td>%s</td><td>%s</td><td>%s</td>",
		//	      username,tmp,ips);
		sb.safePrintf("</tr>");
	}

	// do not print add or del tags buttons if we got tags from more
	// than one TagRec!
	if ( canEdit )
		sb.safePrintf ("<tr bgcolor=#%s><td colspan=10><center>"
			       
			       "<input type=submit name=add "
			       "value=\"Add Tags\" border=0>"
			       
			       "</center></td>"
			       "</tr>\n",DARK_BLUE);

	sb.safePrintf ( "</center></table>" );

	sb.safePrintf ("</form>");

	sb.safePrintf ("</html>");
	
	// clear g_errno, if any, so our reply send goes through
	g_errno = 0;
	// calculate buffer length
	// extract the socket
	//TcpSocket *s = st->m_socket;
	// . nuke the state
	// . first free the buffer, if non-NULL
	//if ( st->m_buf ) mfree ( st->m_buf , st->m_bufLen , "PageTagdb" );
	mdelete ( st , sizeof(State12) , "PageTagdb" );
	delete (st);
	// print it out
	//logf(LOG_DEBUG,"tagdb: %s",sb.getBufStart()+sb.length()-256);
	// . send this page
	// . encapsulates in html header and tail
	// . make a Mime
	return g_httpServer.sendDynamicPage (s, sb.getBufStart(), sb.length());
}

//void classifierDoneWrapper ( void *state ) {
//	g_tagdbClassifier.m_running = false;
//}

// . we can have multiple tags of this type per tag for a single username
// . by default, there can be multiple tags of the same type in the Tag as
//   int32_t as the usernames are all different. see addTag()'s deduping below.
bool isTagTypeUnique ( int32_t tt ) {
	// a dup?
	if ( tt == TT_DUP ) return false; // TT_DUP = 123456
	// make sure table is valid
	if ( ! s_initialized ) g_tagdb.setHashTable();
	// look up in hash table
	TagDesc **tdp = (TagDesc **)s_ht.getValue ( &tt );
	if ( ! tdp ) {
		log("tagdb: tag desc is NULL for tag type %"INT32" assuming "
		    "not indexable",tt);
		return false;
	}
	// do not core for now
	TagDesc *td = *tdp;
	if ( ! td ) {
		log("tagdb: got unknown tag type %"INT32" assuming "
		    "unique",tt);
		return true;
	}
	// if none, that is crazy
	if ( ! td ) { char *xx=NULL;*xx=0; }
	// return 
	if ( td->m_flags & TDF_ARRAY) return false;
	return true;
}

bool isTagTypeIndexable ( int32_t tt ) {
	// a dup?
	if ( tt == TT_DUP ) return false; // TT_DUP = 123456
	// make sure table is valid
	if ( ! s_initialized ) g_tagdb.setHashTable();
	// look up in hash table
	TagDesc **tdp = (TagDesc **)s_ht.getValue ( &tt );
	// do not core for now
	if ( ! tdp ) {
		log("tagdb: got unknown tag type %"INT32" assuming "
		    "not indexable",tt);
		return false;
	}
	TagDesc *td = *tdp;
	if ( ! td ) {
		log("tagdb: tag desc is NULL for tag type %"INT32" assuming "
		    "not indexable",tt);
		return false;
	}
	// if none, that is crazy MDW coring here:
	if ( ! td ) { char *xx=NULL;*xx=0; }
	// return false if we should not index it
	if ( td->m_flags & TDF_NOINDEX ) return false;
	// otherwise, index it
	return true;
}	

// . when displaying a tag we need to know if it is a string or not
// . that and the dataSize determine how we display it
/*
bool isTagTypeString ( int32_t tt ) {
	// look up in hash table
	TagDesc *td = (TagDesc **)s_ht.getValue ( tt );
	// if none, that is crazy
	if ( ! td ) { char *xx=NULL;*xx=0; }
	// return 
	return (td->m_flags & TDF_STRING);
}
*/

// used to determine if one Tag should overwrite the other! if they
// have the same dedup hash... then yes...
int32_t Tag::getDedupHash ( ) {

	// if unique use that!
	if ( isTagTypeUnique ( m_type ) ) return m_type;

	// if we are NOT unique... then hash username and data. thus we only
	// replace a key if its the same tagtype, username and data. that
	// way it will just update the timestamp and/or ip.

	// start hashing here
	char *startHashing = (char *)&m_type;
	// end here. include username (and tag data!)
	char *endHashing = m_buf + m_bufSize;

	// if we are an event tag then PageEvents.cpp added us in the form of
	// user%"UINT64"tag%sval%"INT32" ... so ignore value (FACEBOOKDB)
	//if ( m_type == s_eventTag ) {
	//	endHashing--;
	//	for (;endHashing-1>m_buf&&is_digit(endHashing[-1]);
	//	     endHashing--);
	//}

	// do not include bufsize in hash
	int32_t saved = m_bufSize;
	m_bufSize = 0;

	// hash this many bytes
	int32_t hashSize = endHashing - startHashing;
	// set key
	int32_t dh = hash32 ( startHashing , hashSize );

	// revert bufsize
	m_bufSize = saved;

	return dh;
}

// make sure sizeof(Entry2)=5 not 8!
#pragma pack(1)

class Entry1 {
public:
	uint32_t m_hostHash32;
	uint32_t m_siteNumInlinksUniqueCBlock;
};

class Entry2 {
public:
	uint32_t m_hostHash32;
	uint8_t  m_siteNumInlinksUniqueCBlock;
};

static int linkSort1Cmp ( const void *a, const void *b ) {
	Entry1 *ea = (Entry1 *)a;
	Entry1 *eb = (Entry1 *)b;
	if ( ea->m_hostHash32 > eb->m_hostHash32 ) return  1;
	if ( ea->m_hostHash32 < eb->m_hostHash32 ) return -1;
	return 0;
}

static int linkSort2Cmp ( const void *a, const void *b ) {
	Entry2 *ea = (Entry2 *)a;
	Entry2 *eb = (Entry2 *)b;
	if ( ea->m_hostHash32 > eb->m_hostHash32 ) return  1;
	if ( ea->m_hostHash32 < eb->m_hostHash32 ) return -1;
	return 0;
}

bool Tagdb::loadMinSiteInlinksBuffer ( ) {

	if ( ! loadMinSiteInlinksBuffer2() ) return false;

	// sanity testing
	uint32_t hostHash32 = hash32n("www.imdb.com");
	int32_t msi = getMinSiteInlinks ( hostHash32 );
	if ( msi < 10 ) {
		log("tagdb: bad siteinlinks. linkedin.com not found.");
		//return false;
	}
	hostHash32 = hash32n("0009.org" );
	msi = getMinSiteInlinks ( hostHash32 );
	if ( msi < 0 ) 	{
		log("tagdb: bad siteinlinks. 0009.org not found.");
		//return false;
	}
	// slot #1 in the buffer. make sure b-stepping doesn't lose it between
	// the roundoff error cracks.
	hostHash32 = hash32n("www.hindu.com");
	msi = getMinSiteInlinks ( hostHash32 );
	if ( msi < 3 ) 	{
		log("tagdb: bad siteinlinks. www.hindu.com not found "
		    "(%"INT32").",
		    hostHash32);
		//return false;
	}

	Url tmp;
	tmp.set("gnu.org");
	hostHash32 = tmp.getHash32WithWWW();
	msi = getMinSiteInlinks ( hostHash32 );
	if ( msi < 0 ) 	{
		log("tagdb: bad siteinlinks. www.gnu.org not found.");
		//return false;
	}

	
	return true;
}

bool Tagdb::loadMinSiteInlinksBuffer2 ( ) {

	// use 4 bytes for the first 130,000 entries or so to hold
	// # of site inlinks. then we only need 1 byte since the remaining
	// 25M are <256 sitenuminlinksunqiecblocks
	m_siteBuf1.load(g_hostdb.m_dir,"sitelinks1.dat","stelnks1");
	m_siteBuf2.load(g_hostdb.m_dir,"sitelinks2.dat","stelnks2");

	m_siteBuf1.setLabel("sitelnks");
	m_siteBuf2.setLabel("sitelnks");

	if ( m_siteBuf1.length() > 0 &&
	     m_siteBuf2.length() > 0 ) 
		return true;

	log("gb: loading %ssitelinks.txt",g_hostdb.m_dir);

	// ok, make it
	SafeBuf tmp;
	tmp.load(g_hostdb.m_dir,"sitelinks.txt");
	if ( tmp.length() <= 0 ) {
		log("gb: fatal error. could not find required file "
		    "./sitelinks.txt");
		return false;
	}

	log("gb: starting initial creation of sitelinks1.dat and "
	    "sitelinks2.dat files");

	// now parse each line in that
	char *p = tmp.getBufStart();
	char *pend = p + tmp.length();
	char *newp = NULL;
	SafeBuf buf1;
	SafeBuf buf2;
	int32_t count = 0;
	for ( ; p < pend ; p = newp ) {
		
		if ( ++count % 1000000 == 0 )
			log("gb: parsing line # %"INT32,count);

		// advance to next line
		newp = p;
		for ( ; newp < pend && *newp != '\n' ; newp++ );
		if ( newp < pend ) newp++;
		// parse this line
		int32_t numLinks = atoi(p);
		// skip number
		for ( ; *p && *p != ' ' && *p != '\n' ; p++ );
		// strange
		if ( ! *p || *p == '\n' ) continue;
		// skip spaces
		for ( ; *p == ' ' ; p++ );
		// get hostname
		char *host = p;
		// find end of it
		for ( ; *p && *p != '\n' && *p != ' ' && *p != '\t' ; p++ );
		// hash it
		uint32_t hostHash32 = hash32 ( host , p - host );

		// store in buffer
		if ( numLinks >= 256 ) {
			Entry1 e1;
			e1.m_siteNumInlinksUniqueCBlock = numLinks;
			e1.m_hostHash32 = hostHash32;
			buf1.safeMemcpy ( &e1 , sizeof(Entry1) );
		}
		else {
			Entry2 e2;
			e2.m_siteNumInlinksUniqueCBlock = numLinks;
			e2.m_hostHash32 = hostHash32;
			buf2.safeMemcpy ( &e2 , sizeof(Entry2) );
		}
	}		

	log("gb: sorting sitelink data");

	// now sort each one
	qsort ( buf1.getBufStart() , 
		buf1.length()/sizeof(Entry1),
		sizeof(Entry1),
		linkSort1Cmp );

	qsort ( buf2.getBufStart() , 
		buf2.length()/sizeof(Entry2),
		sizeof(Entry2),
		linkSort2Cmp );


	// now copy to the official buffer so we only alloc what we need
	m_siteBuf1.safeMemcpy ( &buf1 );
	m_siteBuf2.safeMemcpy ( &buf2 );

	log("gb: saving sitelinks1.dat and sitelinks2.dat");

	m_siteBuf1.save(g_hostdb.m_dir,"sitelinks1.dat");
	m_siteBuf2.save(g_hostdb.m_dir,"sitelinks2.dat");

	return true;
}

int32_t Tagdb::getMinSiteInlinks ( uint32_t hostHash32 ) {

	if ( m_siteBuf1.length() <= 0 ) { 
		log("tagdb: load not called");
		char *xx=NULL;*xx=0; 
	}

	// first check buf1 doing bstep
	int32_t ne = m_siteBuf1.length() / sizeof(Entry1);
	Entry1 *ep = (Entry1 *)m_siteBuf1.getBufStart();
	Entry2 *fp = NULL;
	int32_t i = ne / 2;
	int32_t step = ne / 2;
	int32_t count = 0;
	int32_t divs = 0;
	int32_t dir = 0;

 loop1:

	if ( i < 0 ) i = 0;
	if ( i >= ne ) i = ne-1;

	step /= 2;

	if ( step == 1 )
		goto linearScan1;
	if ( hostHash32 < ep[i].m_hostHash32 ) {
		i -= step;
		goto loop1;
	}
	if ( hostHash32 > ep[i].m_hostHash32 ) {
		i += step;
		goto loop1;
	}
	return ep[i].m_siteNumInlinksUniqueCBlock;

 linearScan1:
	if ( hostHash32 < ep[i].m_hostHash32 ) {
		if ( i == 0 ) goto tryNextBuf;
		if ( dir == +1 ) goto tryNextBuf;
		i--;
		dir = -1;
		goto linearScan1;
	}
	if ( hostHash32 > ep[i].m_hostHash32 ) {
		if ( i == ne-1 ) goto tryNextBuf;
		if ( dir == -1 ) goto tryNextBuf;
		i++;
		dir = +1;
		goto linearScan1;
	}
	return ep[i].m_siteNumInlinksUniqueCBlock;


 tryNextBuf:

	// reset parms
	ne = m_siteBuf2.length() / sizeof(Entry2);
	fp = (Entry2 *)m_siteBuf2.getBufStart();
	i = ne / 2;
	step = ne / 2;
	count = 0;
	divs = 0;
	dir = 0;

 loop2:

	if ( i < 0 ) i = 0;
	if ( i >= ne ) i = ne-1;
	step /= 2;
	if ( step == 1 )
		goto linearScan2;
	if ( hostHash32 < fp[i].m_hostHash32 ) {
		i -= step;
		goto loop2;
	}
	if ( hostHash32 > fp[i].m_hostHash32 ) {
		i += step;
		goto loop2;
	}
	return fp[i].m_siteNumInlinksUniqueCBlock;

 linearScan2:

	if ( hostHash32 < fp[i].m_hostHash32 ) {
		if ( i == 0    ) return -1;
		if ( dir == +1 ) return -1;
		i--;
		dir = -1;
		goto linearScan2;
	}
	if ( hostHash32 > fp[i].m_hostHash32 ) {
		if ( i == ne-1 ) return -1;
		if ( dir == -1 ) return -1;
		i++;
		dir = +1;
		goto linearScan2;
	}
	return fp[i].m_siteNumInlinksUniqueCBlock;
}
