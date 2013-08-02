#ifndef _XML_H_
#define _XML_H_

// . this is used for parsing tagdb records
// . used for pasrsing tagdb records, conf file, and html/xml documents
// . NOTE: ALL tags are case INsensitive so <myTag> equals <MYTAG>

#include "XmlNode.h"
#include "Lang.h"

class Xml {

 public:

	Xml  () ;

	// . should free m_xml if m_copy is true
	~Xml () ;

	// do we have any xml in here?
	bool isEmpty () { return (m_xml == NULL); };

	// . set this xml class from a string
	// . should be called before calling anything else
	// . if "copy" makes a copy of the string "s" and references into that
	// . s must be NULL terminated
	// . if it's pure xml then set pureXml to true otherwise we assume it
	//   is html or xhtml
	bool set(char *s , long slen , 
		 bool ownData , long allocSize, //=0, 
		 bool pureXml, // =false );
		 long version ,
		 bool setParents = true,
		 long niceness = 0);


	void  reset ( );
	char *getContent    () { return m_xml;    };
	char *getContentEnd () { return m_xml + m_xmlLen; };
	long  getContentLen () { return m_xmlLen; };
	long  getNumNodes ( ) { return m_numNodes; };
	long  getVersion ( ) { return m_version; };

	// . tagName is compound for xml tags, simple for html tags
	// . xml compound tag name example = "myhouse.bedroom.nightstand"
	// . html simple  tag name example = "title" or "table"
	// . obsolete compound name = myhouse[0].bedroom[2].nightstand[1]
	// . returns -1 if not found
	// . only searches nodes in [n0,n1] node range
	long     getNodeNum  ( 	long n0,
				long n1,
				char *tagName,
				long tagNameLen) const; 
	// some wrappers for it
	long     getNodeNum  ( char *tagName ) {
		if ( ! tagName ) { char *xx=NULL;*xx=0; }
		return getNodeNum ( 0,m_numNodes,tagName,strlen(tagName)); };
	long     getNodeNum  ( char *tagName , long tagNameLen ) {
		return getNodeNum ( 0,m_numNodes,tagName,tagNameLen); };
	long    findNodeNum ( char *nodeText);
	long    getPingServerCount ( ) ;
	// . get the back tag node for a given node
	// . return the last node considered to be a kid or part of node n
	// . used by XmlDoc to parse out <index> tags and find </index>
	//	long getEndNode ( long n );


	bool      isTag         ( long n ) {return m_nodes[n].isTag(); };
	bool      isBreakingTag ( long n ) {return m_nodes[n].m_isBreaking;};
	bool      isBackTag     ( long n ) {return m_nodes[n].m_node[1]=='/';};
	bool      isXmlTag      ( long n ) {return m_nodes[n].m_nodeId == 1;};
	char     *getNode       ( long n ) {return m_nodes[n].m_node; };
	long      getNodeLen    ( long n ) {return m_nodes[n].m_nodeLen;};
	nodeid_t  getNodeId     ( long n ) {return m_nodes[n].m_nodeId;};
	long long getNodeHash   ( long n ) {return m_nodes[n].m_hash;};
	bool      isVisible     ( long n ) {return m_nodes[n].m_isVisible;};

	// get all nodes!
	XmlNode  *getNodes ( ) { return m_nodes; };
	XmlNode  *getNodePtr ( long n ) { return &m_nodes[n]; };

	// . store the the full xml compound name of a tag into "buf" (w/ \0)
	// . return the length stored
	// . ie. "xml.country.state.city"
	// . fullTag option returns the entire node text
	// . ie. "<xml>.<country>.<state abbrev="true">.<city arg="foo">
	long getCompoundName ( long n , char *buf , long bufMaxLen, 
			       bool fullTag = false ) ;

	// . used for parsing xml conf files
	// . used for getting the title in an html doc, etc.
	// . gets the value of the text field immediately following the tag
	// . "tagName" is always compound
	// . only searches nodes in [n0,n1] node range
	bool      getBool     ( long n0 , long n1 , char *tagName ,
				bool      defaultBool     = 0   ); 
	long      getLong     ( long n0 , long n1 , char *tagName ,
			        long      defaultLong     = 0   ); 
	long long getLongLong ( long n0 , long n1 , char *tagName ,
			        long long defaultLongLong = 0LL ); 
	float     getFloat    ( long n0 , long n1 , char *tagName ,
			        float defaultFloat = 0.0 );
	char     *getString   ( long n0 , long n1 , char *tagName , long *len ,
			        bool skipLeadingSpaces = true   ) const; 
	// for parsing facebook replies:
	char     *getNode ( char *tagName , long *len ) ;
	// like above routines but we search all nodes
	bool  getBool     ( char *tagName, bool  defaultBool = false ) {
		return getBool(0,m_numNodes,tagName,defaultBool); }
	long  getLong     ( char *tagName, long  defaultLong = 0 ) {
		return getLong(0,m_numNodes,tagName,defaultLong); }
	long long getLongLong (char *tagName, long long defaultLongLong = 0LL){
		return getLongLong(0,m_numNodes,tagName,defaultLongLong); }
	float getFloat   ( char *tagName, float defaultFloat = 0.0 ) {
		return getFloat(0,m_numNodes,tagName,defaultFloat); }
	char *getString   ( char *tagName                 , 
			    long *len                     , 
			    bool skipLeadingSpaces = true ) const {
		return getString(0,m_numNodes,tagName,len,skipLeadingSpaces); }

	// . used for getting links in the <a href=...> tag
	// . used for getting data from meta tags
	//bool  getBool     ( long node, char *field, bool defaultBool );
	//long  getLong     ( long node, char *field, long defaultLong );
	char *getString   ( long node, char *field, long *valueLen ) {
		if ( node >= m_numNodes ) { char *xx=NULL;*xx=0; }
		return m_nodes[node].getFieldValue ( field , valueLen);}

	// called by getTextForXmlTag() below
	char *getString ( long node , bool skipLeadingSpaces, long *len )const;

	// . like getText() below but gets the content from a meta tag
	// . stores it in "buf"  and NULL terminates it
	// . returns the length
	// . field can be stuff like "summary","description","keywords",...
	// . use "http-equiv" for "name" for meta redirect tags
	// . if "convertHtmlEntites" is true we change < to &lt; and > to &gt;
	long getMetaContent ( char *buf      ,
			      long  bufLen   ,
			      char *field    ,
			      long  fieldLen ,
			      char *name                = "name" ,
			      bool  convertHtmlEntities = false  ,
			      long  startNode   = 0 ,
			      long *matchedNode = NULL ) ;
	
	// just get a pointer to it
	char *getMetaContentPointer ( char *field    ,
				      long  fieldLen ,
				      char *name = "name" ,
				      long *len  = NULL );

	// . filters out tags (uses html entities) and stores in "buf"
	// . replaces "line breaking" html tags with 2 returns
	// . only get text of nodes in [node1,node2]
	// . returns # chars written to buf
	// . buf is NULL terminated
	// . bufMaxSize is usually at least getContentLen() + 1 (m_xmlLen+1)
	// . maxDepth is RELATIVE to node # nodeNumber's depth
	// . if "filter" then convert html entities and \r's to spaces
	// . get kid text of node #"nodeNumber" unless it's -1
	// . if "filterSpaces" then don't allow back to back spaces or \n's
	//   and replace tags with ".." not \n (but no back to back ..'s)
	long  getText ( char  *buf                     , 
			long   bufMaxSize              ,
			long   node1           = 0     ,
			long   node2           = 999999,
			bool   includeTags     = false ,
			bool   visibleTextOnly = true  ,
			bool   filter          = false ,
			bool   filterSpaces    = false ,
			bool   useStopIndexTag = false );

	// do they have a possible search box for gigablast on their page?
	// if url and urlLen are non-null they will be filled in with the 
	// target url
	bool hasGigablastForm(char **url = NULL, long *urlLen = NULL);

	unsigned char getLanguage() { return langUnknown; }

	long  isRSSFeed  ( );
	//bool  isAtomFeed ( );
	//bool  isRDFFeed  ( );
	
	//long  getRSSPublishDate ( long niceness );
	char *getRSSTitle       ( long *titleLen , bool *isHtmlEncoded ) const;
	char *getRSSDescription ( long *titleLen , bool *isHtmlEncoded );

	// get the link to the RSS feed for this page if it has one
	char *getRSSPointer ( long *length,
			      long startNode  = 0,
			      long *matchNode = NULL );

	// get the link from this RSS item to the article
	char *getItemLink ( long *length );

	long getMemUsed() { 
		return m_allocSize + m_maxNumNodes*sizeof(XmlNode); };

	// private:

	// . used by getValueAsBool/Long/String()
	// . tagName is compound for xml tags, simple for html tags
	char *getTextForXmlTag ( long n0, long n1, char *tagName, long *len ,
				 bool skipLeadingSpaces ) const;

	// used because "s" may have words separated by periods
	long long getCompoundHash ( char *s , long len ) const;

	// . set the m_parentNum of each XmlNode in our m_nodes array
	// . TODO: do it on demand?
	void setParents ();

	// . must be called after setParents()
	// . sets the m_compoundHash member variable in each XmlNode in m_nodes
	void setCompoundHashes();

	// . for mapping one xml tag to another
	// . we map single and compound tags
	// . TODO: implement this
	//	class Xml *map;

	XmlNode   *m_nodes;
	long       m_numNodes;
	long       m_maxNumNodes;

	char      *m_xml;
	long       m_xmlLen;

	// If this is a unicode buffer, then m_xml is encoded in UTF-16
	// m_xmlLen is still the size of the buffer IN BYTES
	long       m_version;

	long       m_niceness;

	// if we own the data we free m_xml on reset or destruction
	bool       m_ownData;
	long 	   m_allocSize; // size of buffer, if we allocated it
};

#endif
