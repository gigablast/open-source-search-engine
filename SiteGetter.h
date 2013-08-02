#ifndef _SITEGETTER_H_
#define _SITEGETTER_H_

#include "gb-include.h"
#include "Msg0.h"
#include "Tagdb.h"

#define MAX_SITE_LEN 256

class SiteGetter {

public:

	SiteGetter();
	~SiteGetter();

	// . returns false if blocked, true otherwise
	// . sets g_errno on erorr
	bool getSite ( char         *url      ,
		       class TagRec *gr       ,
		       long          timestamp,
		       char         *coll     ,
		       long          niceness ,
		       //bool          addTags  = false,
		       void         *state    = NULL ,
		       void (* callback)(void *state) = NULL ) ;


	bool setRecognizedSite ( );

	char *getSite    ( ) { return m_site   ; };
	long  getSiteLen ( ) { return m_siteLen; };

	//bool isIndependentSubsite() { return m_isIndependentSubsite; };

	bool getSiteList ( ) ;
	bool gotSiteList ( ) ;
	bool setSite ( ) ;

	class TagRec *m_gr;
	//class Url    *m_url;
	char         *m_url;
	char         *m_coll;
	//bool          m_addTags;
	void         *m_state;
	void        (*m_callback) (void *state );
	RdbList       m_list;

	long          m_sitePathDepth;

	// use Msg0 for getting the no-split termlist that combines 
	// gbpathdepth: with the site hash in a single termid
	Msg0   m_msg0;
	//Msg9a  m_msg9a;
	long   m_pathDepth;
	long   m_maxPathDepth;
	long   m_niceness;
	long   m_oldSitePathDepth;
	char   m_allDone;
	long   m_timestamp;

	bool   m_hasSubdomain;

	// points into provided "u->m_url" buffer
	char   m_site[MAX_SITE_LEN+1];
	long   m_siteLen;

	//bool   m_isIndependentSubsite;

	bool   m_tryAgain;

	long   m_errno;

	// the tag rec we add
	TagRec m_addedTag;
};

#endif
