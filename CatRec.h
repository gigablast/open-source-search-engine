// Matt Wells, copyright Jul 201

// . the record retrieved from tagdb
// . used for describing a site
// . can parse out record from our rdb or from a network msg
// . has siteUrl and filenum of the file that holds the Xml that has the
//   parsing rules and quotas for docs in that site
// . we have the fields you can use at the bottom of this file

#ifndef _CATREC_H_
#define _CATREC_H_

#include "Conf.h"
#include "Xml.h"
#include "RdbList.h"
#include "Tagdb.h"
#include "Categories.h"
#include "Lang.h"
#include "Tagdb.h"
#include "Catdb.h"

#define MAX_IND_CATIDS 1024
#define MAX_SITE_TYPES 12
// url, catids, indirect catids, numCatids, numIndCatids, filenum
#define CATREC_BUF_SIZE MAX_URL_LEN + MAX_CATIDS*4 + 9

class CatRec {

 public:

	// these just set m_xml to NULL
	void reset() ;
	CatRec();
	~CatRec();

	// . extract the site url for "url"
	// . extract the filenum of the file that holds the xml we want
	// . returns false and sets errno on error setting
	// . if rec is NULL we use the default rec for this collection
	bool set ( Url *url, char *data,int32_t dataSize,
		   bool gotByIp ); // , char rdbId = RDB_TAGDB );

	// we're empty if m_xml is NULL
	//bool isEmpty() { return (! m_xml); };

	// . used to by Msg9 to make a CatRec to add
	// . serializes filenum/site into our m_data/m_dataSize
	// . returns false and sets errno on error
	/*
	bool set ( Url *site , char *coll , int32_t collLen , int32_t filenum ,
		   char  version , char rdbId = RDB_TAGDB , int32_t timeStamp = 0,
		   char *comment = NULL, char *username = NULL,
		   int32_t *catids = NULL, unsigned char numCatids = 0, 
		   unsigned char spamBits = 0, char siteQuality = 0, 
		   char adultLevel = 0, 
		   SiteType *siteTypes = NULL, 
		   uint8_t numTypes = 0,
 		   SiteType *langs = NULL, 
 		   uint8_t numLangs = 0); 
	*/
	bool set ( Url *site , int32_t filenum ,
		   int32_t *catids = NULL, unsigned char numCatids = 0 );

	//Xml *getXml() { return m_xml; };

	//bool set ( int32_t filenum ) ;

	//  . this method just sets the filenum, version, url and url-len from
	//  data-pointer "data"
	//  . this method is written as an alternative to the above set methods
	//  Useful if the caller is interested just in the url and url len
	//  saves time
	bool set (char *data, int32_t dataSize);//, char rdbId );

	// set the indirect catids
	void setIndirectCatids ( int32_t *indCatids, int32_t numIndCatids );

	// . did this url have an entry in tagdb?
	// . we need this to know because if it didn't it will have default rec
	// . Msg16 will override Url::isSpam() if this record is not default
	// . Msg25 will also not bother checking for link bans via Msg18
	bool hadRec() { return m_hadRec; };

	// . did we get it by ip? (if not, we got it by canonical domain name)
	// . if we got it by IP and it was banned, admin has the option to
	//   tell gigablast to automatically add the domain name as banned
	//   to tagdb in Msg14.cpp
	bool gotByIp() { return m_gotByIp; };

	// get the record itself (just templateNum/site/coll)
	char *getData     ( ) { return m_data; };
	int32_t  getDataSize ( ) { return m_dataSize; };

	// along with coll/collLen identifies a unique xml file
	//int32_t  getFilenum ( ) { return m_filenum; };
	//int32_t  getRuleset ( ) { return m_filenum; };
	

	// . these should both be NULL terminated
	// . they both reference into the data contained in m_list
	//   or m_buf if the list doesn't have a site record for us
	Url  *getSite          ( ) { return &m_site; };
	//char *getCollection    ( ) { return  m_coll; };
	//int32_t  getCollectionLen ( ) { return  m_collLen; };

	/*
	char* printFormattedRec(char* p);
	void  printFormattedRec(SafeBuf *sb);
	char* printXmlRec      (char* p);
        void  printXmlRec      ( SafeBuf *sb );

	//status of manually set bits.
	bool isSpamUnknown() { return m_spamBits == SPAM_UNKNOWN; }
	bool isSpam()        { return m_spamBits == SPAM_BIT;     }
	bool isNotSpam()     { return m_spamBits == NOT_SPAM;     }
	char* getSpamStr();
	unsigned char getSpamStatus() { return m_spamBits; }

	//
	bool isRatingUnknown()      { return m_adultLevel == NOT_RATED; }
	bool isAdultButNotPorn()    { return m_adultLevel == RATED_R;   }
	bool isPorn()               { return m_adultLevel == RATED_X;   }
	bool isKidSafe()            { return m_adultLevel == RATED_G;   }
	char* getAdultStr();

	char *getPubDateFmtStr();

	int32_t          getTimeStamp()   { return m_timeStamp; }
	char         *getComment()     { return m_comment; }
	char         *getUsername()    { return m_username; }
	char          getSiteQuality() { return m_siteQuality; }
	int32_t          getNumSiteTypes  () { return m_numTypes; }
	int32_t          getNumSiteLangs  () { return m_numLangs; }
	SiteType     *getSiteTypes  () { return m_siteTypes; }
	SiteType     *getSiteLangs  () { return m_siteLangs; }
	uint32_t      getScoreForType(uint8_t type);

	// . mod functions
	// . pain in the butt cuz we gotta change m_data/m_dataSize buffer too
	void          addSiteType    (uint8_t type, uint32_t score ) ;
	void          setFilenum     (int32_t newFilenum );

	// . [n0,n1] constitute an xml node range in "xml"
	// . "len" is the length of another node's data in another xml doc
	// . gets the scoreWeight from docQuality and a node's dataLen
	// . 2nd one gets the maxScore from docQuality
	int32_t getScoreWeightFromQuality ( int32_t n0, int32_t n1, int32_t quality );
	int32_t getScoreWeightFromQuality2( int32_t quality );
	int32_t getMaxScoreFromQuality    ( int32_t n0, int32_t n1, int32_t quality );
	int32_t getMaxLenFromQuality      ( int32_t n0, int32_t n1, int32_t quality );

	//bool hasMaxCountFromQualityTag ( int32_t n0, int32_t n1 ) ;
	//int32_t getMaxCountFromQuality    ( int32_t n0, int32_t n1, int32_t quality ) ;

	int32_t getScoreWeightFromLen     ( int32_t n0, int32_t n1, int32_t len );
	int32_t getScoreWeightFromLen2    ( int32_t len );
	int32_t getScoreWeightFromNumWords( int32_t n0, int32_t n1, int32_t len );
	int32_t getMaxScoreFromLen        ( int32_t n0, int32_t n1, int32_t quality );
	int32_t getMaxScoreFromNumWords   ( int32_t n0, int32_t n1, int32_t quality );

	// 2 new maps for boosting base quality from link statistics
	int32_t getQualityBoostFromNumLinks       ( int32_t numLinks );
	int32_t getQualityBoostFromLinkQualitySum ( int32_t linkBaseQualitySum );

	// 2 new maps for maxScore/scoreWeight of outgoing linkText
	int32_t getLinkTextScoreWeightFromLinkerQuality ( int32_t quality );
	int32_t getLinkTextScoreWeightFromLinkeeQuality ( int32_t quality );
	int32_t getLinkTextMaxScoreFromQuality    ( int32_t quality );
	int32_t getLinkTextScoreWeightFromNumWords( int32_t numWords );


	// . another new map for boosting quality from the link-adjusted 
	//   quality of our root page
	// . root page is just our site url (i.e. http://about.com/)
	// . "rootQuality" is link-adjusted
	int32_t getQualityBoostFromRootQuality ( int32_t rootQuality ) ;

	int32_t getQuotaBoostFromRootQuality ( int32_t rootQuality ) ;
	int32_t getQuotaBoostFromQuality     ( int32_t quality     ) ;

	// if X% of the words are spammed, consider ALL the words to be spammed
	int32_t getMaxPercentForSpamFromQuality ( int32_t quality ) ;

//private:

	// . parses and accesses a map/graph in the xml for us
	// . returns default "def" if map not present or x's in map unordered
	int32_t getY (int32_t n0,int32_t n1,int32_t X,char *strx,char *stry,int32_t def) ;
	*/

	// these reference into m_data???
	Url     m_site;
	//char    m_coll[64];
	//int32_t    m_collLen;

	// filenum determines the xml uniquely
	int32_t    m_filenum;

	// did this rec have it's own entry in tagdb?
	bool    m_hadRec;
	// did we get it by ip? (if not, we got it by canonical domain name)
	bool    m_gotByIp;

	/*
	// . the xml describing this site
	// . references into an Xml stored in Sitedb class
	Xml    *m_xml;
	*/

	// a buffer for holding the little site record itself
	char    m_data[CATREC_BUF_SIZE];
	int32_t    m_dataSize;

	// category ID info for catdb
	unsigned char  m_numCatids;
	int32_t          *m_catids;
	int32_t           m_numIndCatids;
	int32_t           m_indCatids[MAX_IND_CATIDS];

	// version
	unsigned char m_version;
	/*

	
	unsigned char m_spamBits;
	unsigned char m_adultLevel;
	char          m_siteQuality;
	
	uint8_t m_numTypes;
	uint8_t m_numLangs;
	SiteType m_siteTypes[MAX_SITE_TYPES];
	SiteType m_siteLangs[MAX_SITE_TYPES];
	*/

	// url pointer
	char   *m_url;
	int32_t    m_urlLen;

	/*
	// time stamp, comment, username
	int32_t    m_timeStamp;
	char   *m_comment;
	char   *m_username;

	// hack for addSiteType()
	int32_t   *m_incHere;
	char   *m_addHere ;
	// hack for changeFilenum()
	char   *m_filenumPtr;
	*/
};

#endif

// format of a template or default record in xml:

// ## NOTE: the key of the record is the sitename prefixed with the collection:
// ## NOTE: "collectionName:" is prefixed to all hashed terms before hashing
// ## LATER: do permission system

// ## all indexed terms will be preceeded by "collection:" when indexed so you
// ## can do a search within that collection.
// <comment>                  %s  </> 
// ## <addedDate>                %s  </> (stored as a int32_t)
// <allowMimeType>            %s  </> (text, html?) 
// <allowExtension>           %s  </> (used iff allowAllExtensions is false)

// ## the base quality of all docs from this site
// <baseQuality>              %c  </> (0-100%,default 30,qual of docs in site)

// ## the computed link-adjusted quality should not exceed this
// <maxQuality>               %c  </> (0-100%, def 100)

// ## should we treat incoming link text as if it were on our page?
// ## score weights and maxes for the link text is determined by the linker's
// ## own link-adjusted quality. (see graphs/maps below)
// <indexIncomingLinkText>    %b  </> (0-100, default = 100, a %)

// ## do links from this site always point to clean pages?
// <linksClean>               %b  </> (default no)

// ## a doc w/ link-adjusted quality LESS THAN this will not be indexed
// <minQualityToIndex>        %c  </> (default 0%  )  

// ## a doc w/ link-adjusted quality at or below this will be checked for
// ## adult content.
// <maxQualityForAdultDetect> %c  </> (default 0%, 0 means none)

// ## how often do we re-spider it?
// ## we try to compute the best spider rate based on last modified times
// <minSpiderFrequency>       %i  </> (default 60*60*24*30=1month, in seconds)
// <maxSpiderFrequency>       %i  </> (default 60*60*24*30=1month, in seconds)
// <spiderLinks>              %b  </> (default true)
// <spiderLinkPriority>       %"INT32" </> (0-7, default -1) -1 means prntPriorty-1
// <spiderMaxPriority>        %"INT32" </> (0-7, default 7) 


// ## these are fairly self-explanatory
// <maxUrlLen>                %i  </> (default 0, 0 means none)
// <minMetaRefresh>           %i  </> (default 6  )
// <isBanned>                 %b  </> (default no ) 
// <isAdult>                  %b  </> (default no ) 
// <isISP>                    %b  </> (default no ) 
// <isTrusted>                %b  </> (default no ) 
// <allowAdultContent>        %b  </> (default yes)
// <allowCgiUrls>             %b  </> (default yes)
// <allowIpUrls>              %b  </> (default yes)
// <allowAllExtensions>       %b  </> (default yes)
// <allowNonAsciiDocs>        %b  </> (default yes)
// <delete404s>               %b  </> (default yes) from cache/titledb
// <indexDupContent>          %b  </> (default yes)
// <indexSite>                %b  </> (default yes) site:    terms 
// <indexSubSite>             %b  </> (default yes) subsite: terms 
// <indexUrl>                 %b  </> (default yes) url:     terms
// <indexSubUrl>              %b  </> (default yes) suburl:  terms
// <indexIp>                  %b  </> (default yes) ip:      terms
// <indexLinks>               %b  </> (default yes) link:/href: terms

// <maxDocs>                  %ul </> (default -1 = no max)

// ## we don't have a security system... yet...
// ## TODO: <maxCacheSpace>        %ul </> (default 1024*1024)
// ## TODO: <directorMaxScore>     %s  </> (256bit seal for maxScore tag above)

// ## Now for some maps/graphs.
// ## we list the 5 X components followed by the 5 Y components.
// ## all maps/graphs linearly interpolate between the points.
// ## the edge pieces are horizontal.
// ## these maps can have up to 32 points but i typically just use 5.

// ## we map the NUMBER of incoming links to a baseQuality BOOST for our doc.
// ## the resulting new quality is the link-adjusted quality of the linkee doc.
// ## These boosts are ADDED to the existing quality.
// <numLinks11>                %i  </> (default 0   ) 
// <numLinks12>                %i  </> (default 5   )
// <numLinks13>                %i  </> (default 10  )
// <numLinks14>                %i  </> (default 20  )
// <numLinks15>                %i  </> (default 50  )
// <qualityBoost11>            %i  </> (default  0% )
// <qualityBoost12>            %i  </> (default  5% )
// <qualityBoost13>            %i  </> (default 10% )
// <qualityBoost14>            %i  </> (default 15% )
// <qualityBoost15>            %i  </> (default 20% )

// ## we map the SUM of the baseQuality of all linkers to a baseQuality BOOST.
// ## the resulting new quality is the link-adjusted quality of the linkee doc.
// ## we only add up BASE quality of the linkers.
// ## we only add up 1 linker's BASE quality per site.
// ## These boosts are ADDED to the existing quality.
// <linkQualitySum21>          %i  </> (default 0   )
// <linkQualitySum22>          %i  </> (default 50  )
// <linkQualitySum23>          %i  </> (default 100 )
// <linkQualitySum24>          %i  </> (default 150 )
// <linkQualitySum25>          %i  </> (default 200 )
// <qualityBoost21>            %i  </> (default  0% )
// <qualityBoost22>            %i  </> (default  5% )
// <qualityBoost23>            %i  </> (default 10% )
// <qualityBoost24>            %i  </> (default 15% )
// <qualityBoost25>            %i  </> (default 20% )

// ## we map the LINK-ADJUSTED QUALITY of our root page (site url) to a
// ## quality BOOST for us.
// ## the site url is just our site, could be like http://about.com/
// ## These boosts are ADDED to the existing quality.
// <rootQuality31>             %i  </> (default 0   ) 
// <rootQuality32>             %i  </> (default 50  )
// <rootQuality33>             %i  </> (default 100 )
// <rootQuality34>             %i  </> (default 200 )
// <rootQuality35>             %i  </> (default 500 )
// <qualityBoost31>            %i  </> (default  0% )
// <qualityBoost32>            %i  </> (default  5% )
// <qualityBoost33>            %i  </> (default 10% )
// <qualityBoost34>            %i  </> (default 15% )
// <qualityBoost35>            %i  </> (default 20% )

// ## TODO: make based on quality of doc and length of link text!!
// ## currently we limit link text to up to 256 chars in LinkInfo.cpp.
// ## map doc's link-adjusted quality to scoreWeight of it's outgoing link text
// <quality41>                 %i  </> (default   0% )
// <quality42>                 %i  </> (default  30% )
// <quality43>                 %i  </> (default  50% )
// <quality44>                 %i  </> (default  70% )
// <quality45>                 %i  </> (default  85% )
// <linkTextScoreWeight41>     %i  </> (default  50% )
// <linkTextScoreWeight42>     %i  </> (default 100% )
// <linkTextScoreWeight43>     %i  </> (default 130% )
// <linkTextScoreWeight44>     %i  </> (default 180% )
// <linkTextScoreWeight45>     %i  </> (default 250% )

// ## map doc's link-adjusted quality to maxScore of it's outgoing link text.
// ## maxScore applies to all docs from this site as to limit a site's impact.
// <quality51>                 %i  </> (default 
// <quality52>                 %i  </>
// <quality53>                 %i  </>
// <quality54>                 %i  </>
// <quality55>                 %i  </>
// <linkTextMaxScore51>        %i  </>
// <linkTextMaxScore52>        %i  </>
// <linkTextMaxScore53>        %i  </>
// <linkTextMaxScore54>        %i  </>
// <linkTextMaxScore55>        %i  </>

// ## we map the LINK-ADJUSTED QUALITY of our ROOT page (site url) to a quota
// ## boost. (can be negative)
// ## the site url is just our site, could be like http://about.com/
// ## These boosts are MULTIPLIED by the existing quota.
// <rootQuality71>             %i  </> (default 0   ) 
// <rootQuality72>             %i  </> (default 50  )
// <rootQuality73>             %i  </> (default 100 )
// <rootQuality74>             %i  </> (default 200 )
// <rootQuality75>             %i  </> (default 500 )
// <quotaBoost71>              %i  </> (default  0% )
// <quotaBoost72>              %i  </> (default  0% )
// <quotaBoost73>              %i  </> (default  0% )
// <quotaBoost74>              %i  </> (default  0% )
// <quotaBoost75>              %i  </> (default  0% )

// ## we map the LINK-ADJUSTED QUALITY of our page (site url) to a quota
// ## boost. (can be negative)
// ## the site url is just our site, could be like http://about.com/
// ## These boosts are MULTIPLIED by the existing quota.
// <quality81>                 %i  </> (default 0   ) 
// <quality82>                 %i  </> (default 50  )
// <quality83>                 %i  </> (default 100 )
// <quality84>                 %i  </> (default 200 )
// <quality85>                 %i  </> (default 500 )
// <quotaBoost81>              %i  </> (default  0% )
// <quotaBoost82>              %i  </> (default  0% )
// <quotaBoost83>              %i  </> (default  0% )
// <quotaBoost84>              %i  </> (default  0% )
// <quotaBoost85>              %i  </> (default  0% )

// ## the <index> node describes parsing/indexing rtu
// ## used for xhtml tags (title, meta summary/keywords/description)
// ## NOTE: <score2> <weight2> defines a point on the #words-to-score function
// ## NOTE: omit <name> to index whole body (exculdes meta tags and xml tags)
// ## NOTE: set  <name> to "meta.summary" for indexing meta tag summary
// ## NOTE: set  <name> to "meta.keywords" for indexing meta tag keywords
// ## NOTE: set  <name> to "meta.description" for indexing meta tag keywords
// ## NOTE: set  <name> to "Xml" for indexing ALL xml tags
// ## NOTE: set  <name> to ??? for indexing text under that tag <???>...</>
//  <index>
//    <name>                     %s      </> ("title","meta.summary","Xml","W")
//    <indexAsName>              %s      </> (for mapping pure xml tags)
//    <prefix>                   %s      </> (like "title", "myTag:" -can omit)
//    <maxQualityForSpamDetect>  %c      </> (default 0, 0 means none)
//    <minQualityToIndex>        %ul     </> (0-255, default 0  ) do not index
//    <minDepth>                 %ul     </> (0-inf, default 0  )
//    <maxDepth>                 %ul     </> (0-inf, default inf)
//    <maxLenToIndex>            %ul     </> (0-inf, default inf)
//    <indexAllOccurences>       %b      </> (default no) (ex.: no for title)
//    <indexCRC>                 %b      </> (default no ) index checksum?
//    <filterHtmlEntities>       %b      </> (default yes)
//    <indexIfUniqueOnly>        %b      </> (default no ) hash word iff unique
//    <indexSingletons>          %b      </> (default yes)
//    <indexPhrases>             %b      </> (default yes)
//    <indexAsWhole>             %b      </> (default no ) hash a checksum
//    <useStopWords>             %b      </> (default yes)
//    <useStems>                 %b      </> (default yes)
//
//    ## Map doc's (link-adjusted) quality to a maxLen for this field.
//    ## 30% quality is probably average.
//    ## NOTE: there really are no defaults for these, use tagdb default rec.
//    <quality11>                 %c  </> (default 15% )
//    <quality12>                 %c  </> (default 30% )
//    <quality13>                 %c  </> (default 45% )
//    <quality14>                 %c  </> (default 60% )
//    <quality15>                 %c  </> (default 80% )
//    <maxLen11>                  %ul </> (default 80k )
//    <maxLen12>                  %ul </> (default 100k)
//    <maxLen13>                  %ul </> (default 150k)
//    <maxLen14>                  %ul </> (default 200k)
//    <maxLen15>                  %ul </> (default 250k)
//
//    ## Map doc's (link-adjusted) quality to a maxScore for this field.
//    <quality21>                 %c  </> (default 15% )
//    <quality22>                 %c  </> (default 30% )
//    <quality23>                 %c  </> (default 45% )
//    <quality24>                 %c  </> (default 60% )
//    <quality25>                 %c  </> (default 80% )
//    <maxScore21>                %ul </> (default 30% )
//    <maxScore22>                %ul </> (default 45% )
//    <maxScore23>                %ul </> (default 60% )
//    <maxScore24>                %ul </> (default 80% )
//    <maxScore25>                %ul </> (default 100%)
//
//    ## map doc (link-adjusted) quality to a scoreWeight for this field
//    <quality31>                 %c  </> (default 15% )
//    <quality32>                 %c  </> (default 30% )
//    <quality33>                 %c  </> (default 45% )
//    <quality34>                 %c  </> (default 60% )
//    <quality35>                 %c  </> (default 80% )
//    <scoreWeight31>             %ul </> (default 60% )
//    <scoreWeight32>             %ul </> (default 100%)
//    <scoreWeight33>             %ul </> (default 150%)
//    <scoreWeight34>             %ul </> (default 200%)
//    <scoreWeight35>             %ul </> (default 250%)
//
//    ## map field length to a scoreWeight for this field
//    <len41>                    %ul  </> (default 100) #w<100 -->wght=300
//    <len42>                    %ul  </> (default 500) score in[200,300]
//    <len43>                    %ul  </> (default 1000)
//    <len44>                    %ul  </> (default 2000)
//    <len45>                    %ul  </> (default 5000) if under/over 5000
//    <scoreWeight41>            %ul  </> (default 300%) 
//    <scoreWeight42>            %ul  </> (default 200%) 
//    <scoreWeight43>            %ul  </> (default 150%) 
//    <scoreWeight44>            %ul  </> (default 100%) 
//    <scoreWeight45>            %ul  </> (default  50%) 
//
//    ## map field length to a maxScore for this field
//    <len51>                    %ul  </> (default 100) #w<100 -->wght=300
//    <len52>                    %ul  </> (default 500) score in[200,300]
//    <len53>                    %ul  </> (default 1000)
//    <len54>                    %ul  </> (default 2000)
//    <len55>                    %ul  </> (default 5000) if under/over 5000
//    <maxScore51>               %ul  </> (default 30% )
//    <maxScore52>               %ul  </> (default 45% )
//    <maxScore53>               %ul  </> (default 60% )
//    <maxScore54>               %ul  </> (default 80% )
//    <maxScore55>               %ul  </> (default 100%)
//
//  </>

// TODO:
// <indexAsLong>, <indexAsBool>, ... for pure xml tags w/ special meaning
// 
