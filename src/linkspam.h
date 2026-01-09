// Matt Wells, copyright Nov 2001

#ifndef _LINKSPAM_H_
#define _LINKSPAM_H_

#include "gb-include.h"
#include "ip.h"
//#include "TermTable.h"

bool setLinkSpam ( int32_t             ip                 ,
                   int32_t            *indCatIds          ,
                   int32_t             numIndCatIds       ,
                   class Url       *linker             ,
                   int32_t             siteNumInlinks     ,
		   class Xml       *xml                ,
		   class Links     *links              ,
		   bool             isContentTruncated ,
		   int32_t             niceness           );

bool isLinkSpam  ( class Url       *linker         ,
		   int32_t             ip             ,
		   int32_t            *indCatIds      ,
		   int32_t             numIndCatIds   ,
		   int32_t             siteNumInlinks ,
		   class Xml       *xml            ,
		   class Links     *links          ,
		   int32_t             maxDocLen      , 
		   char           **note           , 
		   Url             *linkee         ,
		   int32_t             linkNode       , 
		   char            *coll           ,
		   int32_t             niceness       );

char *getCommentSection ( char *haystack     ,
			  int32_t  haystackSize ,
			  bool  isUnicode    ,
			  int32_t  niceness     );
#endif
