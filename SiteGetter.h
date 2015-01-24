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
		       int32_t          timestamp,
		       collnum_t collnum,
		       int32_t          niceness ,
		       //bool          addTags  = false,
		       void         *state    = NULL ,
		       void (* callback)(void *state) = NULL ) ;


	bool setRecognizedSite ( );

	char *getSite    ( ) { return m_site   ; };
	int32_t  getSiteLen ( ) { return m_siteLen; };

	//bool isIndependentSubsite() { return m_isIndependentSubsite; };

	bool getSiteList ( ) ;
	bool gotSiteList ( ) ;
	bool setSite ( ) ;

	class TagRec *m_gr;
	//class Url    *m_url;
	char         *m_url;
	collnum_t m_collnum;
	//bool          m_addTags;
	void         *m_state;
	void        (*m_callback) (void *state );
	RdbList       m_list;

	int32_t          m_sitePathDepth;

	// use Msg0 for getting the no-split termlist that combines 
	// gbpathdepth: with the site hash in a single termid
	Msg0   m_msg0;
	//Msg9a  m_msg9a;
	int32_t   m_pathDepth;
	int32_t   m_maxPathDepth;
	int32_t   m_niceness;
	int32_t   m_oldSitePathDepth;
	char   m_allDone;
	int32_t   m_timestamp;

	bool   m_hasSubdomain;

	// points into provided "u->m_url" buffer
	char   m_site[MAX_SITE_LEN+1];
	int32_t   m_siteLen;

	//bool   m_isIndependentSubsite;

	bool   m_tryAgain;

	int32_t   m_errno;

	// the tag rec we add
	TagRec m_addedTag;
};

#endif
