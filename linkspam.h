// Matt Wells, copyright Nov 2001

#ifndef _LINKSPAM_H_
#define _LINKSPAM_H_

#include "gb-include.h"
#include "ip.h"
//#include "TermTable.h"

bool setLinkSpam ( long             ip                 ,
                   long            *indCatIds          ,
                   long             numIndCatIds       ,
                   class Url       *linker             ,
                   long             siteNumInlinks     ,
		   class Xml       *xml                ,
		   class Links     *links              ,
		   bool             isContentTruncated ,
		   long             niceness           );

bool isLinkSpam  ( class Url       *linker         ,
		   long             ip             ,
		   long            *indCatIds      ,
		   long             numIndCatIds   ,
		   long             siteNumInlinks ,
		   class Xml       *xml            ,
		   class Links     *links          ,
		   long             maxDocLen      , 
		   char           **note           , 
		   Url             *linkee         ,
		   long             linkNode       , 
		   char            *coll           ,
		   long             niceness       );

char *getCommentSection ( char *haystack     ,
			  long  haystackSize ,
			  bool  isUnicode    ,
			  long  niceness     );
#endif
