#ifndef _PAGERESULTS_H_
#define _PAGERESULTS_H_

#include "SafeBuf.h"

bool printDmozRadioButtons ( SafeBuf &sb , long catId ) ;
bool printLogoAndSearchBox ( SafeBuf &sb , class HttpRequest *hr, long catId );

bool printTermPairs ( SafeBuf &sb , class Query *q , class PairScore *ps ) ;
bool printSingleTerm ( SafeBuf &sb , class Query *q , class SingleScore *ss );


bool printEventAddress ( SafeBuf &sb , char *addrStr , class SearchInput *si ,
			 double *lat , double *lon , bool isXml ,
			 // use this for printing distance if lat/lon above
			 // is invalid. only for non-xml printing though.
			 float zipLat ,
			 float zipLon ,
			 double eventGeocoderLat,
			 double eventGeocoderLon,
			 char *eventBestPlaceName );

bool printDMOZCrumb ( SafeBuf &sb , long catId , bool xml ) ;
bool printDMOZSubTopics ( SafeBuf&  sb, long catId, bool inXml ) ;

bool printEventCountdown2 ( SafeBuf &sb ,
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
