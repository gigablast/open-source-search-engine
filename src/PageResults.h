#ifndef _PAGERESULTS_H_
#define _PAGERESULTS_H_

#include "SafeBuf.h"
#include "Language.h" // MAX_FRAG_SIZE
#include "Msg40.h"
#include "Msg0.h"

// height of each search result div in the widget
#define RESULT_HEIGHT 120
// other widget parms
#define SERP_SPACER 1
#define PADDING 8
#define SCROLLBAR_WIDTH 20

bool printCSVHeaderRow2 ( class SafeBuf *sb ,
			  int32_t ct ,
			  class CollectionRec *cr ,
			  class SafeBuf *nameBuf ,
			  class HashTableX *columnTable ,
			  class Msg20 **msg20s ,
			  int32_t numMsg20s ,
			  int32_t *numPtrsArg ) ;

class State0 {
public:

	// store results page in this safebuf
	SafeBuf      m_sb;

	// if socket closes before we get a chance to send back
	// search results, we will know by comparing this to
	// m_socket->m_numDestroys
	int32_t         m_numDestroys;
	bool         m_header;

	collnum_t    m_collnum;
	//Query        m_q;
	SearchInput  m_si;
	Msg40        m_msg40;
	TcpSocket   *m_socket;
	Msg0         m_msg0;
	int64_t    m_startTime;
	//Ads          m_ads;
	bool         m_gotAds;
	bool         m_gotResults;
	char         m_spell  [MAX_FRAG_SIZE]; // spelling recommendation
	bool         m_gotSpell;
	int32_t         m_errno;
	Query        m_qq3;
        int32_t         m_numDocIds;
	int64_t    m_took; // how long it took to get the results
	HttpRequest  m_hr;
	bool         m_printedHeaderRow;
	//char         m_qe[MAX_QUERY_LEN+1];
	SafeBuf m_qesb;

	// for printing our search result json items in csv:
	HashTableX   m_columnTable;
	int32_t         m_numCSVColumns;

	// stuff for doing redownloads
	bool    m_didRedownload;
	XmlDoc *m_xd;
	int32_t    m_oldContentHash32;
	int64_t m_socketStartTimeHack;
};


bool printSearchResultsHeader ( class State0 *st ) ;
bool printResult ( class State0 *st,  int32_t ix , int32_t *numPrintedSoFar );
bool printSearchResultsTail ( class State0 *st ) ;




bool printDmozRadioButtons ( SafeBuf *sb , int32_t catId ) ;
bool printLogoAndSearchBox ( SafeBuf *sb , class HttpRequest *hr, int32_t catId ,
			     SearchInput *si );

bool printTermPairs ( SafeBuf *sb , class Query *q , class PairScore *ps ) ;
bool printSingleTerm ( SafeBuf *sb , class Query *q , class SingleScore *ss );


bool printEventAddress ( SafeBuf *sb , char *addrStr , class SearchInput *si ,
			 double *lat , double *lon , bool isXml ,
			 // use this for printing distance if lat/lon above
			 // is invalid. only for non-xml printing though.
			 float zipLat ,
			 float zipLon ,
			 double eventGeocoderLat,
			 double eventGeocoderLon,
			 char *eventBestPlaceName );

bool printDMOZCrumb ( SafeBuf *sb , int32_t catId , bool xml ) ;
bool printDMOZSubTopics ( SafeBuf *sb, int32_t catId, bool inXml ) ;

bool printEventCountdown2 ( SafeBuf *sb ,
			    SearchInput *si,
		       int32_t now ,
		       int32_t timeZoneOffset ,
		       char useDST,
		       int32_t nextStart ,
		       int32_t nextEnd ,
		       int32_t prevStart ,
		       int32_t prevEnd ,
		       bool storeHours ,
			    bool onlyPrintIfSoon ) ;

char **getEventCategories();

#endif
