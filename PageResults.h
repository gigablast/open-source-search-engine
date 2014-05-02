#ifndef _PAGERESULTS_H_
#define _PAGERESULTS_H_

#include "SafeBuf.h"
#include "Language.h" // MAX_FRAG_SIZE
#include "Msg40.h"
#include "Msg0.h"

// height of each search result div in the widget
#define RESULT_HEIGHT 120

class State0 {
public:

	// store results page in this safebuf
	SafeBuf      m_sb;

	// if socket closes before we get a chance to send back
	// search results, we will know by comparing this to
	// m_socket->m_numDestroys
	long         m_numDestroys;
	bool         m_header;

	collnum_t    m_collnum;
        Query        m_q;
	SearchInput  m_si;
	Msg40        m_msg40;
	TcpSocket   *m_socket;
	Msg0         m_msg0;
	long long    m_startTime;
	//Ads          m_ads;
	bool         m_gotAds;
	bool         m_gotResults;
	char         m_spell  [MAX_FRAG_SIZE]; // spelling recommendation
	bool         m_gotSpell;
	long         m_errno;
	Query        m_qq3;
        long         m_numDocIds;
	long long    m_took; // how long it took to get the results
	HttpRequest  m_hr;
	bool         m_printedHeaderRow;
	char         m_qe[MAX_QUERY_LEN+1];

	// for printing our search result json items in csv:
	HashTableX   m_columnTable;
	long         m_numCSVColumns;

	// stuff for doing redownloads
	bool    m_didRedownload;
	XmlDoc *m_xd;
	long    m_oldContentHash32;
};


bool printSearchResultsHeader ( class State0 *st ) ;
bool printResult ( class State0 *st,  long ix , long numPrintedSoFar );
bool printSearchResultsTail ( class State0 *st ) ;




bool printDmozRadioButtons ( SafeBuf *sb , long catId ) ;
bool printLogoAndSearchBox ( SafeBuf *sb , class HttpRequest *hr, long catId );

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

bool printDMOZCrumb ( SafeBuf *sb , long catId , bool xml ) ;
bool printDMOZSubTopics ( SafeBuf *sb, long catId, bool inXml ) ;

bool printEventCountdown2 ( SafeBuf *sb ,
			    SearchInput *si,
		       long now ,
		       long timeZoneOffset ,
		       char useDST,
		       long nextStart ,
		       long nextEnd ,
		       long prevStart ,
		       long prevEnd ,
		       bool storeHours ,
			    bool onlyPrintIfSoon ) ;

char **getEventCategories();

#endif
