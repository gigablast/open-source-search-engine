#ifndef _XMLNODE_H_
#define _XMLNODE_H_

#include "gb-include.h"
// . an xml node can be text or tag (html or xml tag)

typedef int16_t nodeid_t;

// . get how many xml/html tags we have classified in our g_nodes[] array
// . used by Weights.cpp
int32_t getNumXmlNodes ( ) ;
bool isBreakingTagId ( nodeid_t tagId ) ;
bool hasBackTag ( nodeid_t tagId ) ;
int32_t getTagLen ( char *node ) ;
bool isTagStart ( char *s );//, int32_t i, int32_t version ) ;
// s points to tag name - first char
nodeid_t getTagId ( char *s , class NodeType **retp = NULL ); 

class XmlNode {

 public:

	friend class Xml;    // needs to access our private parts ;)
	friend class XmlDoc; // needs to access our private parts ;)

	bool  isText       () { return m_nodeId == 0; };
	bool  isTag        () { return m_nodeId >  0; };
	bool  isHtmlTag    () { return m_nodeId >  1; };
	bool  isXmlTag     () { return m_nodeId == 1; };
	nodeid_t getNodeId    () { return m_nodeId; };
	int64_t getNodeHash() { return m_hash; };
	char *getNode      () { return m_node; };
	// m_nodeLen is in bytes
	int32_t  getNodeLen   () { return m_nodeLen; };
	//int32_t  getXmlParent () { return m_xmlParentTagNum; };
	bool  isBreaking   () { return m_isBreaking; };
	bool  isVisible    () { return m_isVisible; };
	bool  hasBackTag   () { return m_hasBackTag; };

	// exclude meta tags and comment tags (they are not front or back)
	bool  isFrontTag () { 
		return m_nodeId > 0 && m_node[1]!='/' &&
			m_nodeId != 68 && m_nodeId != 109; };

	// . get the value of a field like "href" in the <a href="blah"> tag
	char *getFieldValue ( char *fieldName , int32_t *valueLen );

	// . used exclusively by Xml class which contains an array of XmlNodes
	// . "node" points to the beginning of the node, the '<' if it's a tag
	// . sets m_node,m_nodeLen,m_hash,m_isBreaking,m_nodeId
	// . returns the length of the node
	// . pureXml is true if node cannot be an html tag, except comment
	//int32_t set ( char *node , bool pureXml );
	int32_t set ( char *node , bool pureXml , int32_t version );

	// private:

	// . called by set() to get the length of a tag node
	//int32_t getTagLen      ( char *node , int32_t version);
	//int32_t getTagLen      ( UChar *node , int32_t version );

	// . called by set() to get the length of a TEXT node (and set it)
	//int32_t setTextNode    ( char *node );

	// . called by set() to get the length of a COMMENT node (and set it)
	int32_t setCommentNode ( char *node );
	//int32_t setCommentNode ( UChar *node );

	int32_t setCommentNode2 ( char *node );

	// . called by set() to get the length of a CDATA node (and set it)
	int32_t setCDATANode ( char *node );
	//int32_t setCDATANode ( UChar *node );

	// . called by set() to get nodeId and isBreaking of a tag node
	// . returns the nodeId
	nodeid_t setNodeInfo    ( int64_t  nodeHash );

	char      *m_node;             // tag data, or text data if not a tag
	int32_t       m_nodeLen;          // m_nodeLen is in bytes
	char      *m_tagName;          // iff this node is a tag
	int32_t       m_tagNameLen;
	int64_t  m_hash;             // iff this node is a tag
	//int64_t  m_compoundHash;     // set by Xml class
	//int32_t       m_parentTagNum;     // set by Xml class
	//int32_t       m_xmlParentTagNum;  // set by Xml class
	int16_t      m_depth;            // set by Xml class (xml depth only)
	nodeid_t   m_nodeId;           // 0 for text,1 for xml tag, 1+ for html
	char       m_hasBackTag:1;
	char       m_isBreaking:1;     // does tag (if it is) line break?
	char       m_isVisible:1;
	char       m_isSelfLink:1;  // an a href tag link to self?
	int32_t       m_pairTagNum;    // paired opening or closing tag
	// . "m_linkNum" references a link in Links.cpp
	// . use for <a href> xml nodes only right now
	// . used so XmlDoc.cpp::getContactUsLink() works better
	//int32_t       m_linkNum;        
	class XmlNode *m_parent;
};

// . does "s" start a tag? (regular tag , back tag or comment tag)
inline bool isTagStart ( char *s ) { // , int32_t i, int32_t version ) {
	// it must start with < to be a tag
	if ( *s != '<' ) return false;
	// a <gb is a fake tag because we now decode all html entites
	// so in htmlDecode() in fctypes.cpp we decode &lt; to
	// "<gb"
	//if ( s[i+1]=='g' && s[i+2]=='b') return false;
	// minimal tag is 3 chars
	// if ( !s[ii + 2 >= len ) return false;
	// next char can be an alnum, !-- or / then alnum
	if ( is_alnum_a ( s[1] ) ) return true;
	// next char can be 1 of 3 things to be a tag
	//switch ( s[1] ) {
	// / is also acceptable, followed only by an alnum or >
	if ( s[1]== '/' ) {
		if ( is_alnum_a(s[2]) ) return true;
		if ( s[2] == '>'    ) return true;
		return false;
	}
	// office.microsoft.com uses <?xml ...?> tags
	if ( s[1]=='?' ) {
		if ( is_alnum_a(s[2]) ) return true;
		//if ( s[2] == '>'    ) return true; <?> is tag???
		return false;
	}
	// make sure the double hyphens follow the ! or alnum
	if ( s[1]=='!' ) {
		// this is for <!xml> i guess
		if ( is_alnum_a(s[2]) ) return true;
		// and the <![CDATA[
		if ( s[2]=='[' && s[3]=='C' && s[4]=='D' &&
		     s[5]=='A' && s[6]=='T' && s[7]=='A' &&
		     s[8]=='[' ) return true;
		// and the <!-- comment here--> famous comment tag
		if ( s[2]=='-' && s[3]=='-' ) return true;
		// and <![....]> i've seen too
		// <![if gt IE 6]><script>.... for waterfordcoc.org
		if ( s[2] == '[' ) return true;
	}
	return false;
};


// Now set up a structure for describing ALL the available HTML nodes.
// . Each HTML node has a name, name length, does it break a word?
//   a format bit. (most HTML tags have 0 for their format bit
//   because we really don't care about what they do -- we use format
//   bits for extracting title, summaries, et al.
// . the is indexable is false for tags like <script> <option> whose contents
//   are not visible/indexable
class NodeType {
 public:
	char    *m_nodeName;
	bool     m_hasBackTag;
	char     m_isBreaking;
	char     m_isVisible;
	char     m_filterKeep1; // for &strip=1 option
	char     m_filterKeep2; // for &strip=2 option
	nodeid_t m_nodeId;
	char     m_isXmlTag;
};

extern class NodeType g_nodes[];

inline char *getTagName ( nodeid_t tagId ) {return g_nodes[tagId].m_nodeName;};

// . each tag has a number
enum {
	TAG_TEXTNODE = 0,
	TAG_XMLTAG,
	TAG_A,
	TAG_ABBREV,
	TAG_ACRONYM,
	TAG_ADDRESS,
	TAG_APPLET,
	TAG_AREA,
	TAG_AU,
	TAG_AUTHOR,
	TAG_B, // 10
	TAG_BANNER,
	TAG_BASE,
	TAG_BASEFONT,
	TAG_BGSOUND,
	TAG_BIG,
	TAG_BLINK,
	TAG_BLOCKQUOTE,
	TAG_BQ,
	TAG_BODY,
	TAG_BR, // 20
	TAG_CAPTION,
	TAG_CENTER,
	TAG_CITE,
	TAG_CODE,
	TAG_COL,
	TAG_COLGROUP,
	TAG_CREDIT,
	TAG_DEL,
	TAG_DFN,
	TAG_DIR, // 30
	TAG_DIV,
	TAG_DL,
	TAG_DT,
	TAG_DD,
	TAG_EM,
	TAG_EMBED,
	TAG_FIG,
	TAG_FN,
	TAG_FONT,
	TAG_FORM, // 40
	TAG_FRAME,
	TAG_FRAMESET,
	TAG_H1,
	TAG_H2,
	TAG_H3,
	TAG_H4,
	TAG_H5,
	TAG_H6,
	TAG_HEAD,
	TAG_HR, // 50
	TAG_HTML,
	TAG_I,
	TAG_IFRAME,
	TAG_IMG,
	TAG_INPUT,
	TAG_INS,
	TAG_ISINDEX,
	TAG_KBD,
	TAG_LANG,
	TAG_LH, // 60
	TAG_LI,
	TAG_LINK,
	TAG_LISTING,
	TAG_MAP,
	TAG_MARQUEE,
	TAG_MATH,
	TAG_MENU,
	TAG_META,
	TAG_MULTICOL,
	TAG_NOBR, // 70
	TAG_NOFRAMES,
	TAG_NOTE,
	TAG_OL,
	TAG_OVERLAY,
	TAG_P,
	TAG_PARAM,
	TAG_PERSON,
	TAG_PLAINTEXT,
	TAG_PRE,
	TAG_Q, // 80
	TAG_RANGE,
	TAG_SAMP,
	TAG_SCRIPT,
	TAG_SELECT,
	TAG_SMALL,
	TAG_SPACER,
	TAG_SPOT,
	TAG_STRIKE,
	TAG_STRONG,
	TAG_SUB, // 90
	TAG_SUP,
	TAG_TAB,
	TAG_TABLE,
	TAG_TBODY,

	TAG_TD,
	TAG_TEXTAREA,
	TAG_TEXTFLOW,
	TAG_TFOOT,
	TAG_TH,
	TAG_THEAD, // 100
	TAG_TITLE,
	TAG_TR,
	TAG_TT,

	TAG_U,
	TAG_UL,
	TAG_VAR,
	TAG_WBR,
	TAG_XMP,
	TAG_COMMENT,

	TAG_OPTION, // 110
	TAG_STYLE,
	TAG_DOCTYPE,
	TAG_XML,
	TAG_START,
	TAG_STOP,
	TAG_SPAN,
	TAG_LEGEND,
	TAG_S,

	TAG_ABBR,
	TAG_CDATA, // 120
	TAG_NOSCRIPT,
	TAG_FIELDSET,
	TAG_FBORIGLINK, // "feedburner:origlink" special feedburner link
	TAG_RDF  ,      // rdf:RDF
	TAG_RSS  ,      // rss
	TAG_FEED ,      // atom feed tag

	TAG_ITEM,
	TAG_ENTRY,
	TAG_CHANNEL,
	TAG_ENCLOSURE,
	TAG_WEBLOG,
	// a tag we insert in XmlDoc.cpp to indicate expanded frame/iframe src
	TAG_GBFRAME,
	TAG_TC,
	TAG_GBXMLTITLE,

	// facebook xml tags
	TAG_FBSTARTTIME, // 135
	TAG_FBENDTIME, // 136
	TAG_FBNAME,
	TAG_FBPICSQUARE,
	TAG_FBHIDEGUESTLIST,

	// . do not parse this up into words!! it is text in <script> tags
	// . consider it a whole tag i guess
	TAG_SCRIPTTEXT,
	TAG_BUTTON,
	TAG_URLFROM, // for ahrefs.com

	// support sitemap.xml
	TAG_LOC,

	//
	// fake tags below here
	//
	// a fake tag used by Sections.cpp
	TAG_SENTENCE,

	LAST_TAG
};
#endif


