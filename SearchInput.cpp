#include "gb-include.h"

#include "SearchInput.h"
#include "Parms.h"         // g_parms
#include "CollectionRec.h" // cr
#include "Pages.h"         // g_msg
#include "LanguageIdentifier.h"
#include "CountryCode.h"
#include "geo_ip_table.h"
#include "Users.h"
#include "Address.h" // getLatLonFromUserInput
#include "Timedb.h"
#include "PageResults.h"

SearchInput::SearchInput() {
	reset();
}
SearchInput::~SearchInput() {
	reset();
}
void SearchInput::reset ( ) {
	/*
	m_langHint = 0;
	m_languageWeightFactor = 0.33;
	m_enableLanguageSorting = 0;
	m_queryIP = 0;
	m_hr = NULL;
	m_gbcountry = NULL;
	m_gbcountryLen = 0;
	m_country = 0;
	m_language = 0;
	m_sq = NULL;
	m_sqLen = 0;
	m_noDocIds     = NULL;
	m_noSiteIds    = NULL;
	m_noDocIdsLen  = 0;
	m_noSiteIdsLen = 0;
	*/
}

void SearchInput::setToDefaults ( CollectionRec *cr , long niceness ) {
	// reset it first
	reset();
	// set all to 0 just to avoid any inconsistencies
	long size = (char *)&m_END_TEST - (char *)&m_START;
	memset ( this , 0x00 , size );
	m_sbuf1.reset();
	m_sbuf2.reset();
	m_sbuf3.reset();

	// set these
	m_numLinesInSummary  = 2;
	m_docsWanted         = 10;
	m_boolFlag           = 2;
	m_maxQueryTerms      = 1000;
	m_niceness           = niceness;

	m_defaultSortLanguageLen = 0;
}


// . make a key for caching the search results page based on this input
// . do not use all vars, like the m_*ToDisplay should not be included
key_t SearchInput::makeKey ( ) {
	// hash the query
	long       n       = m_q->getNumTerms  ();
	long long *termIds = m_q->getTermIds   ();
	char      *signs   = m_q->getTermSigns ();
	key_t k;
	k.n1 = 0;
	k.n0 = hash64 ( (char *)termIds , n * sizeof(long long) );
	k.n0 = hash64 ( (char *)signs   , n , k.n0 );
	// user defined weights, for weighting each query term separately
	for ( long i = 0 ; i < n ; i++ ) {
		k.n0 = hash64 ((char *)&m_q->m_qterms[i].m_userWeight,4, k.n0);
		k.n0 = hash64 ((char *)&m_q->m_qterms[i].m_userType  ,1, k.n0);
	}
	// space separated, NULL terminated, list of meta tag names to display
	if ( m_displayMetas          ) 
		k.n0 = hash64b ( m_displayMetas          , k.n0 );
	// name of collection in external cluster to get titleRecs for 
	// related pages from
	if ( m_rp_getExternalPages && m_rp_externalColl )
		k.n0 = hash64b ( m_rp_externalColl , k.n0 );
	// collection e import from
	if ( m_importColl )
		k.n0 = hash64b ( m_importColl , k.n0 );
	// the special query parm
	if ( m_sq && m_sqLen > 0 )
		k.n0 = hash64 ( m_sq , m_sqLen , k.n0 );
	if ( m_noDocIds && m_noDocIdsLen )
		k.n0 = hash64 ( m_noDocIds , m_noDocIdsLen , k.n0 );
	if ( m_noSiteIds && m_noSiteIdsLen )
		k.n0 = hash64 ( m_noSiteIds , m_noSiteIdsLen , k.n0 );

	// no need to hash these again separately, they are in between 
	// m_START and m_END_HASH
	// language
	//if ( m_language )
	//	k.n0 = hash64 ( m_language , k.n0 );
	//if ( m_gblang )
	//	k.n0 = hash64 ( m_gblang , k.n0 );
	// . now include the hash of the search parameters
	// . nnot incuding m_docsToScanForTopics since since we got TopicGroups
	char *a = ((char *)&m_START) + 4 ; // msg40->m_dpf;
	char *b =  (char *)&m_END_HASH   ; // msg40->m_topicGroups;
	long size = b - a; 
	// push and flush some parms that should not contribute
	//long save1 = m_refs_numToDisplay;
	//long save2 = m_rp_numToDisplay;
	//long save3 = m_numTopicsToDisplay;
	//m_refs_numToDisplay  = 0;
	//m_rp_numToDisplay    = 0;
	//m_numTopicsToDisplay = 0;
	// and hash it all up
	k.n0 = hash64 ( a , size , k.n0 );
	// and pop out the parms that did not contribute
	//m_refs_numToDisplay  = save1;
	//m_rp_numToDisplay    = save2;
	//m_numTopicsToDisplay = save3;
	// hash each topic group
	for ( long i = 0 ; i < m_numTopicGroups ; i++ ) {
		TopicGroup *t = &m_topicGroups[i];
		//k.n0 = hash64 ( t->m_numTopics           , k.n0 );
		k.n0 = hash64 ( t->m_maxTopics           , k.n0 );
		k.n0 = hash64 ( t->m_docsToScanForTopics , k.n0 );
		k.n0 = hash64 ( t->m_minTopicScore       , k.n0 );
		k.n0 = hash64 ( t->m_maxWordsPerTopic    , k.n0 );
		k.n0 = hash64b( t->m_meta                , k.n0 );
		k.n0 = hash64 ( t->m_delimeter           , k.n0 );
		k.n0 = hash64 ( t->m_useIdfForTopics     , k.n0 );
		k.n0 = hash64 ( t->m_dedup               , k.n0 );
	}
	// . boolean queries have operators (AND OR NOT ( ) ) that we need
	//   to consider in this hash as well. so
	// . so just hash the whole damn query
	if ( m_q->m_isBoolean ) {
		char *q    = m_q->getQuery();
		long  qlen = m_q->getQueryLen();
		k.n0 = hash64 ( q , qlen , k.n0 );
	}

	// Language stuff
	k.n0 = hash64(m_defaultSortLanguage, m_defaultSortLanguageLen, k.n0);
	k.n0 = hash64(m_defaultSortCountry , m_defaultSortCountryLen , k.n0);

	// debug
	//logf(LOG_DEBUG,"query: q=%s k.n0=%llu",m_q->getQuery(),k.n0);

	//Msg1aParms* m1p = msg40->getReferenceParms();
	//if( m1p ) {
	//	k.n0=hash64(((char*)m1p)+sizeof(long), 
	//		    sizeof(Msg1aParms)-8,k.n0);
	//}
	return k;
}

void SearchInput::test ( ) {
	// set all to 0 just to avoid any inconsistencies
	char *a = ((char *)&m_START) + 4 ; // msg40->m_dpf;
	char *b =  (char *)&m_END_TEST;
	long size = b - a;
	memset ( a , 0x00 , size );
	// loop through all possible cgi parms to set SearchInput
	for ( long i = 0 ; i < g_parms.m_numSearchParms ; i++ ) {
		Parm *m = g_parms.m_searchParms[i];
		char *x = (char *)this + m->m_soff;
		if ( m->m_type != TYPE_BOOL ) *(long *)x = 0xffffffff;
		else                          *(char *)x = 0xff;
	}
	// ensure we're all zeros now!
	long fix = a - (char *)this;
	unsigned char *p = (unsigned char *)a;
	for ( long i = 0 ; i < size ; i++ ) {
		if ( p[i] == 0xff ) continue;
		// find it
		long off = i + fix;
		char *name = NULL; // "unknown";
		for ( long k = 0 ; k < g_parms.m_numSearchParms ; k++ ) {
			Parm *m = g_parms.m_searchParms[k];
			if ( m->m_soff != off ) continue;
			name = m->m_title;
			break;
		}
		if ( ! name ) continue;
		log("query: Got uncovered SearchInput parm at offset "
		    "%li in SearchInput. name=%s.",off,name);
	}
}

void SearchInput::copy ( class SearchInput *si ) {
	memcpy ( (char *)this , (char *)si , sizeof(SearchInput) );
}

class SearchInput *g_si = NULL;

bool SearchInput::set ( TcpSocket *sock , HttpRequest *r , Query *q ) {

	// get coll rec
	long  collLen;
	char *coll = r->getString ( "c" , &collLen );
	//if (! coll){coll = g_conf.m_defaultColl; collLen = gbstrlen(coll); }
	if ( ! coll )
		coll = g_conf.getDefaultColl(r->getHost(), r->getHostLen());
	if ( ! coll ) { g_errno = ENOCOLLREC; return false; }
	collLen = gbstrlen(coll);
	CollectionRec *cr = g_collectiondb.getRec ( coll );
	if ( ! cr ) { 
		g_errno = ENOCOLLREC;
		g_msg = " (error: no such collection)";		
		return false;
	}

	// set all to 0 just to avoid any inconsistencies
	//long size = (char *)&m_END_TEST - (char *)&m_START;
	//memset ( this , 0x00 , size );
	setToDefaults( cr , 0 ); // niceness

	m_cr = cr;

	m_coll2    = m_cr->m_coll;
	m_collLen2 = gbstrlen(m_coll2);

	// from ::reset()
	m_languageWeightFactor = 0.33;

	// Set IP for language detection.
	// (among other things)
	if ( sock ) m_queryIP = sock->m_ip;
	else        m_queryIP = 0;
	m_hr = r;

	// keep ptr to the query class to use
	m_q        = q;

	// set this here since its size can be variable
	m_sq = r->getString("sq",&m_sqLen);
	// negative docids
	m_noDocIds = r->getString("nodocids",&m_noDocIdsLen);
	// negative sites
	m_noSiteIds = r->getString("nositeids",&m_noSiteIdsLen);

	// Msg5e calls Msg40 with this set to true in the searchInput
	// so it can analyze the entire pages of each search result so it
	// can find the article start/end tag sequence indicators
	m_getTitleRec = r->getLong("gettrs",0);

	m_getSitePops = r->getLong("getsitepops",0 );

        // does this collection ban this IP?
	/*
	long  encapIp = 0; 
m	if (! cr->hasSearchPermission ( sock, encapIp ) ) {
		g_errno = ENOPERM;
		g_msg = " (error: permission denied)";
		return false;
	}
	*/

	// set all search parms in SearchInput to defaults
	for ( long i = 0 ; i < g_parms.m_numSearchParms ; i++ ) {
		Parm *m = g_parms.m_searchParms[i];
		// sanity
		if ( m->m_soff < 0 ) { char *xx=NULL;*xx=0; }
		char *x = (char *)this + m->m_soff;
		// what is the def val ptr
		char *def = NULL;
		if      ( m->m_off >= 0 && m->m_obj == OBJ_COLL )
			def = ((char *)cr) + m->m_off;
		else if ( m->m_off >= 0 && m->m_obj == OBJ_CONF )
			def = ((char *)&g_conf) + m->m_off;
		// set it based on type
		if      ( m->m_type == TYPE_LONG ) {
			long v = 0;
			if ( def )
				v = *(long *)def;
			else if ( m->m_def ) 
				v = atol(m->m_def);
			*(long *)x = v;
		}
		else if ( m->m_type == TYPE_BOOL ) {
			long v = 0;
			if ( def ) 
				v = *(char *)def;
			else if ( m->m_def ) 
				v = atol(m->m_def);
			// sanity test!
			if ( v != 0 && v != 1 )
				log("query: got non-bool default "
				    "for bool parm %s",m->m_title);
			if ( v ) *(char *)x = 1;
			else     *(char *)x = 0;
		}
		else if ( m->m_type == TYPE_CHAR ) {
			if ( def )
				*(char *)x = *(char *)def;
			else if ( m->m_def ) 
				*(char *)x = atol(m->m_def);
		}
		else if ( m->m_type == TYPE_FLOAT ) {
			float v = 0;
			if ( def )
				v = *(float *)def;
			else if ( m->m_def ) 
				v = atof(m->m_def);
			*(float *)x = (float)v;
		}
		else if ( m->m_type == TYPE_STRING ||
			  m->m_type == TYPE_STRINGBOX ) {
			//if ( m->m_cgi && strcmp ( m->m_cgi, "erpc" ) == 0 )
			//	log("hey1");
			//if ( m->m_cgi && strcmp ( m->m_scgi, "q" ) == 0 )
			//	log("hey1");
			char *v = NULL;
			if ( def )
				v = (char *)def;
			else if ( m->m_def ) 
				v = m->m_def;
			*(char **)x = v;
			// set the length
			if ( ! v ) *(long *)(x-4) = 0;
			else       *(long *)(x-4) = gbstrlen(v);
		}
	}

	// this is just used to determine in PageResults.cpp if we should
	// show admin knobs next to each result...
	// default to off for now
	m_isAdmin = r->getLong("admin",0);
	//if ( m_isAdmin ) m_isAdmin = g_users.hasPermission ( r , PAGE_MASTER );
	// local ip?
	if ( ! r->isLocal() ) m_isAdmin = 0;

	// default set does not take into account g_conf,
	// so we will take care of that here ourselves...
	m_adFeedEnabled  = g_conf.m_adFeedEnabled;
	//m_excludeLinkText = g_conf.m_excludeLinkText;
	//m_excludeMetaText = g_conf.m_excludeMetaText;

	// we need to get some cgi values in order to correct the defaults
	// based on if we're doing an xml feed, have a site: query, etc.
	long  xml      = r->getLong ( "xml" , 0 ); // was "raw"
	long  siteLen  = 0; r->getString ("site",&siteLen);
	long  sitesLen = 0; r->getString ("sites",&sitesLen);
	
	// now override automatic defaults for special cases
	if ( xml > 0 ) {
		m_familyFilter            = 0;
		// this is causing me a headache when on when i dont know it
		m_restrictIndexdbForQuery   = false;
		// this is hackish
		if ( r->getLong("rt",0) ) m_restrictIndexdbForQuery=false;
		m_numTopicsToDisplay      = 0;
		m_doQueryHighlighting     = 0;
		m_spellCheck              = 0;
		m_refs_numToGenerate      = 0;
		m_refs_docsToScan         = 0;
	}
	else if ( m_siteLen > 0 ) {
		m_restrictIndexdbForQuery = false;
		m_doSiteClustering        = false;
		m_ipRestrictForTopics     = false;
	}
	else if ( m_sitesLen > 0 ) {
		m_ipRestrictForTopics     = false;
	}

	m_doIpClustering          = false;
	m_sitesQueryLen           = 0;

	// set the user ip, "uip"
	long uip = m_queryIP;
	char *uipStr = m_hr->getString ("uip" , NULL );
	long tmpIp = 0; if ( uipStr ) tmpIp = atoip(uipStr);
	if ( tmpIp ) uip = tmpIp;

	//
	//
	// BEGIN MAIN PARM SETTING LOOP
	//
	//

	// loop through all possible cgi parms to set SearchInput
	for ( long i = 0 ; i < g_parms.m_numSearchParms ; i++ ) {
		Parm *m = g_parms.m_searchParms[i];
		char *x = (char *)this + m->m_soff;
		// what is the parm's cgi name?
		char *cgi = m->m_scgi;
		if ( ! cgi ) cgi = m->m_cgi;
		// sanity check
		if ( ! m->m_sparm ) {
			log("query: Failed search input sanity check.");
			char *xx = NULL; *xx = 0;
		}
		// . break it down by type now
		// . get it from request and store it in SearchInput
		if ( m->m_type == TYPE_LONG ) {
			// default was set above
			long def = *(long *)x;
			// assume default
			long v = def;
			// but cgi parms override cookie
			v = r->getLong ( cgi , v );
			// but if its a privledged parm and we're not an admin
			// then do not allow overrides, but m_priv of 3 means
			// to not display for clients, but to allow overrides
			if ( ! m_isAdmin && m->m_priv && m->m_priv!=3) v = def;
			// bounds checks
			if ( v < m->m_smin ) v = m->m_smin;
			if ( v > m->m_smax ) v = m->m_smax;
			if ( m->m_sminc >= 0 ) {
				long vmin = *(long *)((char *)cr+m->m_sminc);
				if ( v < vmin ) v = vmin;
			}
			if ( m->m_smaxc >= 0 ) {
				long vmax = *(long *)((char *)cr+m->m_smaxc);
				if ( v > vmax ) v = vmax;
			}
			// set it
			*(long *)x = v;
			// do not print start result num (m->m_sprop is 0 for 
			// "s" now)
			//if ( cgi[0] == 's' && cgi[1] == '\0' ) continue;
			// should we propagate it? true by default
			//if ( ! m->m_sprop ) continue;
			// if it is the same as its default, and the default is
			// always from m_def and never from the CollectionRec, 
			// then do not both storing it in here! what's the 
			// point?
			if ( v == def && m->m_off < 0 ) continue;
			// if not default do not propagate
			if ( v == def ) continue;
			// . include for sure if explicitly provided
			// . vp will be NULL if "cgi" is not explicitly listed 
			//   as a cgi parm. otherwise, even if *vp == '\0', vp
			//   is non-NULL.
			// . crap, it can be in the cookie now
			//char *vp = r->getValue(cgi, NULL, NULL);
			// if not given at all, do not propagate
			//if ( ! vp ) continue;
			// store in up if different from default, even if
			// same as default ("def") because default may be
			// changed by the admin since m->m_off >= 0
			//if ( m->m_sprpg && up + gbstrlen(cgi) + 20 < upend ) 
			//	up += sprintf ( up , "%s=%li&", cgi , v );
			//if ( m->m_sprpp && pp + gbstrlen(cgi) + 80 < ppend )
			//	pp += sprintf ( pp , "<input type=hidden "
			//			"name=%s value=\"%li\">\n", 
			//			cgi , v );
		}
		else if ( m->m_type == TYPE_FLOAT ) {
			// default was set above
			float def = *(float *)x;
			// get overriding from http request, if any
			float v;
			// but if its a privledged parm and we're not an admin
			// then do not allow overrides
			if ( ! m_isAdmin && m->m_priv && m->m_priv!=3) v = def;
			else v = r->getFloat( cgi , def );
			// bounds checks
			if ( v < m->m_smin ) v = m->m_smin;
			if ( v > m->m_smax ) v = m->m_smax;
			if ( m->m_sminc >= 0 ) {
				float vmin = *(float *)((char *)cr+m->m_sminc);
				if ( v < vmin ) v = vmin;
			}
			if ( m->m_smaxc >= 0 ) {
				float vmax = *(float *)((char *)cr+m->m_smaxc);
				if ( v > vmax ) v = vmax;
			}
			// set it
			*(float *)x = v;
			// do not print start result num
			//if ( cgi[0] == 's' && cgi[1] == '\0' ) continue;

			// include for sure if explicitly provided
			char *vp = r->getValue(cgi, NULL, NULL);
			if ( ! vp ) continue;
			// unchanged from default?
			if ( v == def ) continue;
			// store in up different from default
			//if ((vp||v!= def) && up + gbstrlen(cgi)+20 < upend ) 
			//	up += sprintf ( up , "%s=%f&", cgi , v );
			//if ((vp||v!= def) && pp + gbstrlen(cgi)+20 < ppend )
			//	pp += sprintf ( pp , "<input type=hidden "
			//			"name=%s value=\"%f\">\n", 
			//			cgi , v );
		}

		else if ( m->m_type == TYPE_BOOL ) {
			// default was set above
			long def = *(char *)x;
			if ( def != 0 ) def = 1; // normalize
			// assume default
			long v = def;
			// cgi parms override cookie
			v = r->getBool ( cgi , v );
			// but if no perm, use default
			if ( ! m_isAdmin && m->m_priv && m->m_priv!=3) v = def;
			if ( v != 0 ) v = 1; // normalize
			*(char *)x = v;
			// don't propagate rcache
			//if ( ! strcmp(cgi,"rcache") ) continue;
			// should we propagate it? true by default
			//if ( ! m->m_sprop ) continue;
			// if it is the same as its default, and the default is
			// always from m_def and never from the CollectionRec, 
			// then do not both storing it in here! what's the 
			// point?
			if ( v == def && m->m_off < 0 ) continue;
			// if not default do not propagate
			if ( v == def ) continue;
			// . include for sure if explicitly provided
			// . vp will be NULL if "cgi" is not explicitly listed 
			//   as a cgi parm. otherwise, even if *vp == '\0', vp 
			//   is non-NULL.
			// . crap, it can be in the cookie now!
			//char *vp = r->getValue(cgi, NULL, NULL);
			// if not given at all, do not propagate
			//if ( ! vp ) continue;
			// store in up if different from default, even if
			// same as default ("def") because default may be
			// changed by the admin since m->m_off >= 0
			//if ( m->m_sprpg && up + gbstrlen(cgi) + 10 < upend )
			//	up += sprintf ( up , "%s=%li&", cgi , v );
			//if ( m->m_sprpp && pp + gbstrlen(cgi) + 80 < ppend )
			//	pp += sprintf ( pp , "<input type=hidden "
			//			"name=%s value=\"%li\">\n", 
			//			cgi , v );
		}
		else if ( m->m_type == TYPE_CHAR ) {
			// default was set above
			char def = *(char *)x;
			*(char *)x = r->getLong ( cgi, def );
			// use this
			long v = *(char *)x;
			// store in up if different from default, even if
			// same as default ("def") because default may be
			// changed by the admin since m->m_off >= 0. nah,
			// let's try to reduce cgi parm pollution...
			if ( v == def ) continue;
			//if ( m->m_sprpg && up + gbstrlen(cgi) + 10 < upend )
			//	up += sprintf ( up , "%s=%li&", cgi , v );
			//if ( m->m_sprpp && pp + gbstrlen(cgi) + 80 < ppend )
			//	pp += sprintf ( pp , "<input type=hidden "
			//			"name=%s value=\"%li\">\n", 
			//			cgi , v );
		}
		else if ( m->m_type == TYPE_STRING ||
			  m->m_type == TYPE_STRINGBOX ) {
			//if ( m->m_cgi && strcmp ( m->m_cgi, "qlang" ) == 0 )
			//	log("hey2");
			char *def = *(char **)x;
			// get overriding from http request, if any
			long len = 0;
			char *v = NULL;
			// . cgi parms override cookie
			// . is this url encoded?
			v = r->getString ( cgi , &len , v );
			// if not specified explicitly, default it and continue
			if ( ! v ) {
				// sanity
				if  ( ! def ) def = "";
				*(char **)x = def;
				// length preceeds char ptr in SearchInput
				*(long *)(x - 4) = gbstrlen(def);
				continue;
			}
			// if something was specified, override, it might
			// be length zero, too
			*(char **)x = v;
			// length preceeds char ptr in SearchInput
			*(long *)(x - 4) = len;
			// do not store if query, that needs to be last so
			// related topics can append to it
			//if ( cgi[0] == 'q' && cgi[1] == '\0' ) continue;
			// should we propagate it? true by default
			//if ( ! m->m_sprop ) continue;
			// if not given at all, do not propagate
			//if ( ! vp ) continue;
			// if it is the same as its default, and the default is
			// always from m_def and never from the CollectionRec, 
			// then do not both storing it in here! what's the 
			// point?
			//if ( v && v == def && !strcmp(def,v) && m->m_off < 0)
			//	continue;
			// Need to set qcs based on page encoding...
			// not propagated
			if (!strncmp(cgi, "qcs", 3))
				continue;
			// do not propagate defaults
			if ( v == def ) continue;
			// store in up if different from default, even if
			// same as default ("def") because default may be
			// changed by the admin since m->m_off >= 0
			//if( m->m_sprpg && up+gbstrlen(cgi)+len+6  < upend ) {
			//	up += sprintf ( up , "%s=", cgi );
			//	up  += urlEncode ( up , upend-up-2 , v , len );
			//	*up++ = '&';
			//}
			// propogate hidden inputs
			//if ( m->m_sprpp && up+gbstrlen(cgi)+len+80 < upend )
			//	pp += sprintf ( pp , "<input type=hidden "
			//			"name=%s value=\"%s\">\n", 
			//			cgi , v );
		}
	}

	// now add the special "qh" parm whose default value changes 
	// depending on if we are widget related or not
	long qhDefault = 1;
	m_doQueryHighlighting = r->getLong("qh",qhDefault);


	//
	// TODO: use Parms.cpp defaults
	//
	TopicGroup *tg = &m_topicGroups[0];

	//
	//
	// gigabits
	//
	//
	tg->m_numTopics = 50;
	tg->m_maxTopics = 50;
	tg->m_docsToScanForTopics = m_docsToScanForTopics;
	tg->m_minTopicScore = 0;
	tg->m_maxWordsPerTopic = 6;
	tg->m_meta[0] = '\0';
	tg->m_delimeter = '\0';
	tg->m_useIdfForTopics = false;
	tg->m_dedup = true;
	// need to be on at least 2 pages!
	tg->m_minDocCount = 2;
	tg->m_ipRestrict = true;
	tg->m_dedupSamplePercent = 80;
	tg->m_topicRemoveOverlaps = true;
	tg->m_topicSampleSize = 4096;
	// max sequential punct chars allowedin a topic
	tg->m_topicMaxPunctLen = 1;
	m_numTopicGroups = 1;

	// use "&dg=1" to debug gigabits
	m_debugGigabits = r->getLong("dg",0);


	// . omit scoring info from the xml feed for now
	// . we have to roll this out to gk144 net i think
	if ( xml > 0 )
		m_getDocIdScoringInfo = 0;

	// turn off by default!
	if ( ! r->getLong("gigabits",0) ) {
		m_numTopicGroups = 0;
	}


	//////////////////////////////////////
	//
	// transform input into classes
	//
	//////////////////////////////////////

	// USER_ADMIN, ...
	m_username = g_users.getUsername(r);
	// if collection is NULL default to one in g_conf
	if ( ! m_coll2 || ! m_coll2[0] ) { 
		//m_coll = g_conf.m_defaultColl; 
		m_coll2 = g_conf.getDefaultColl(r->getHost(), r->getHostLen());
		m_collLen2 = gbstrlen(coll); 
	}

	// reset this
	m_gblang = 0;

	// use gblang then!
	long gglen;
	char *gg = r->getString ( "clang" , &gglen , NULL );
	if ( gg && gglen > 1 )
		m_gblang = getLanguageFromAbbr(gg);

	// allow for "qlang" if still don't have it
	//long gglen2;
	//char *gg2 = r->getString ( "qlang" , &gglen2 , NULL );
	//if ( m_gblang == 0 && gg2 && gglen2 > 1 )
	//	m_gblang = getLanguageFromAbbr(gg2);
	
	// fix query by removing lang:xx from ask.com queries
	//char *end = m_query + m_queryLen -8;
	//if ( m_queryLen > 8 && m_query && end > m_query && 
	//     strncmp(end," lang:",6)==0 ) {
	//	char *asklang = m_query+m_queryLen - 2;
	//	m_gblang = getLanguageFromAbbr(asklang);
	//	m_queryLen -= 8;
	//	m_query[m_queryLen] = 0;
	//	
	//}

	// . returns false and sets g_errno on error
	// . sets m_qbuf1 and m_qbuf2
	if ( ! setQueryBuffers () ) return false;


	/* --- Virtual host language detection --- */
	if(r->getHost()) {
		bool langset = getLanguageFromAbbr(m_defaultSortLanguage);
		char *cp;
		if(!langset && (cp = strrchr(r->getHost(), '.'))) {
			uint8_t lang = getLanguageFromUserAgent(++cp);
			if(lang) {
				// char langbuf[128];
				// sprintf(langbuf, "qlang=%s\0", getLanguageAbbr(lang));
				//m_defaultSortLanguage = getLanguageAbbr(lang);
                                char *tmp = getLanguageAbbr(lang);
                                strncpy(m_defaultSortLanguage, tmp, 6);
				// log(LOG_INFO,
				//	getLanguageString(lang), r->getHost(), this);
			}
		}
	}
	/* --- End Virtual host language detection --- */

	char *qs1 = m_defaultSortLanguage;

	// this overrides though
	//long qlen2;
	//char *qs2 = r->getString ("qlang",&qlen2,NULL);
	//if ( qs2 ) qs1 = qs2;
	
	m_queryLang = getLanguageFromAbbr ( qs1 );

	if ( qs1 && qs1[0] && ! m_queryLang )
		log("query: qlang of \"%s\" is NOT SUPPORTED",qs1);





	// . the query to use for highlighting... can be overriden with "hq"
	// . we need the language id for doing synonyms
	if ( m_highlightQuery && m_highlightQuery[0] )
		m_hqq.set2 ( m_highlightQuery , m_queryLang , true );
	else if ( m_query && m_query[0] )
		m_hqq.set2 ( m_query , m_queryLang , true );

	// log it here
	log("query: got query %s",m_sbuf1.getBufStart());

	// . now set from m_qbuf1, the advanced/composite query buffer
	// . returns false and sets g_errno on error (ETOOMANYOPERANDS)
	if ( ! m_q->set2 ( m_sbuf1.getBufStart(), 
			   m_queryLang , 
			   m_queryExpansion ) ) {
		g_msg = " (error: query has too many operands)";
		return false;
	}

	if ( m_q->m_truncated && m_q->m_isBoolean ) {
		g_errno = ETOOMANYOPERANDS;
		g_msg = " (error: query has too many operands)";
		return false;
	}


	// do not allow querier to use the links: query operator unless they 
	// are admin or the search controls explicitly allow links:
	//if ( m_q->m_hasLinksOperator && ! m_isAdmin  &&
	//     !cr->m_allowLinksSearch ) {
	//	g_errno = ENOPERM;
	//	g_msg = " (error: permission denied)";
	//	return false;
	//}

	// miscellaneous
	m_showBanned = false;
	//if ( m_isAdmin ) m_showBanned = true;
	// admin can say &sb=0 explicitly to not show banned results
	if ( m_isAdmin ) m_showBanned = r->getLong("sb",m_showBanned);


	if ( m_q->m_hasUrlField  ) m_ipRestrictForTopics = false;
	if ( m_q->m_hasIpField   ) {
		m_ipRestrictForTopics = false;
		//if( m_isAdmin ) m_showBanned = true;
	}
	if ( m_q->m_hasPositiveSiteField ) {
		m_ipRestrictForTopics = false;
		m_doSiteClustering    = false;
	}
	if ( m_q->m_hasQuotaField ) {
		m_doSiteClustering    = false;
		m_doDupContentRemoval = false;
	}



	long codeLen;
	char *code = r->getString ("code",&codeLen,NULL);
	// set m_endUser
	if ( ! codeLen || ! code || strcmp(code,"gbfront")==0 )
		m_endUser = true;
	else
		m_endUser = false;


	if(codeLen && !m_endUser) {
		m_maxResults = cr->m_maxSearchResultsForClients;
	}
	else {
		m_maxResults = cr->m_maxSearchResults;
	}
	// don't let admin bewilder himself
	if ( m_maxResults < 1 ) m_maxResults = 500;

	// we can't get this kind of constraint from generic Parms routines
	if ( m_firstResultNum + m_docsWanted > m_maxResults ) 
		m_firstResultNum = m_maxResults - m_docsWanted;
	if(m_firstResultNum < 0) m_firstResultNum = 0;

	// if useCache is -1 then pick a default value
	if ( m_useCache == -1 ) {
		// assume yes as default
		m_useCache = 1;
		// . if query has url: or site: term do NOT use cache by def.
		// . however, if spider is off then use the cache by default
		if ( g_conf.m_spideringEnabled ) {
			if      ( m_q->m_hasPositiveSiteField ) m_useCache = 0;
			else if ( m_q->m_hasIpField   ) m_useCache = 0;
			else if ( m_q->m_hasUrlField  ) m_useCache = 0;
			else if ( m_siteLen  > 0      ) m_useCache = 0;
			else if ( m_sitesLen > 0      ) m_useCache = 0;
			else if ( m_urlLen   > 0      ) m_useCache = 0;
		}
	}
	// never use cache if doing a rerank (msg3b)
	//if ( m_rerankRuleset >= 0 ) m_useCache = 0;
	bool readFromCache = false;
	if ( m_useCache ==  1  ) readFromCache = true;
	if ( m_rcache   ==  0  ) readFromCache = false;
	if ( m_useCache ==  0  ) readFromCache = false;
	// if useCache is false, don't write to cache if it was not specified
	if ( m_wcache == -1 ) {
		if ( m_useCache ==  0 ) m_wcache = 0;
		else                    m_wcache = 1;
	}
	// save it
	m_rcache = readFromCache;

	/*
	m_language = 0;
	// convert m_languageCode to a number for m_language
	if ( m_languageCode ) {
		m_language = (unsigned char)atoi(m_languageCode);
		if ( m_language == 0 )
			m_language = getLanguageFromAbbr(m_languageCode);
	}
	*/

	// a hack for buzz for backwards compatibility
	//if ( strstr ( m_q->m_orig,"gbkeyword:r36p1" ) )
	//	m_ruleset = 36;

	//
	// . turn this off for now
	// . it is used in setClusterLevels() to use clusterdb to filter our
	//   search results via Msg39, so it is not the most efficient.
	// . plus i am deleting most foreign language pages from the index
	//   so we can just focus on english and that will give us more english
	//   pages that we could normally get. we don't have resources to
	//   de-spam the other languages, etc.
	// . turn it back on, i took out the setClusterLevels() use of that
	//   because we got the langid in the posdb keys now
	//
	//m_language = 0;

	// convert m_defaultSortCountry to a number for m_countryHint
	m_countryHint = g_countryCode.getIndexOfAbbr(m_defaultSortCountry);


	return true;
}

// . sets m_qbuf1[] and m_qbuf2[]
// . m_qbuf1[] is the advanced query
// . m_qbuf2[] is the query to be used for spell checking
// . returns false and set g_errno on error
bool SearchInput::setQueryBuffers ( ) {

	m_sbuf1.reset();
	m_sbuf2.reset();
	m_sbuf3.reset();

	short qcs = csUTF8;
	if (m_queryCharset && m_queryCharsetLen){
		// we need to convert the query string to utf-8
		qcs = get_iana_charset(m_queryCharset, m_queryCharsetLen);
		if (qcs == csUnknown) {
			//g_errno = EBADCHARSET;
			//g_msg = "(error: unknown query charset)";
			//return false;
			qcs = csUTF8;
		}
	}
	// prepend sites terms
	long numSites = 0;
	char *csStr = NULL;
	numSites = 0;
	csStr = get_charset_str(qcs);

	if ( m_sites && m_sites[0] ) {
		char *s = m_sites;
		char *t;
		long  len;
		m_sbuf1.pushChar('(');//*p++ = '(';
	loop:
		// skip white space
		while ( *s && ! is_alnum_a(*s) ) s++;
		// bail if done
		if ( ! *s ) goto done;
		// get length of it
		t = s;
		while ( *t && ! is_wspace_a(*t) ) t++;
		len = t - s;
		// add site: term
		//if ( p + 12 + len >= pend ) goto toobig;
		if ( numSites > 0 ) m_sbuf1.safeStrcpy ( " UOR " );
		m_sbuf1.safeStrcpy ( "site:" );
		//p += ucToUtf8(p, pend-p,s, len, csStr, 0,0);
		m_sbuf1.safeMemcpy ( s , len );
		//memcpy ( p , s , len     ); p += len;
		//*p++ = ' ';
		m_sbuf1.pushChar(' ');
		s = t;
		numSites++;
		goto loop;
	done:
		m_sbuf1.safePrintf(") | ");
		// inc totalLen
		m_sitesQueryLen = m_sitesLen + (numSites * 10);
	}
	// append site: term
	if ( m_siteLen > 0 ) {
		//if ( p > pstart ) *p++ = ' ';
		if ( m_sbuf1.length() ) m_sbuf1.pushChar(' ');
		//memcpy ( p , "+site:" , 6 ); p += 6;
		m_sbuf1.safePrintf("+site:");
		//memcpy ( p , m_site , m_siteLen ); p += m_siteLen;
		m_sbuf1.safeMemcpy(m_site,m_siteLen);
	}


	// append gblang: term
	if( m_gblang > 0 ) {
		//if( p > pstart ) *p++ =  ' ';
		if ( m_sbuf1.length() ) m_sbuf1.pushChar(' ');
		//p += sprintf( p, "+gblang:%li |", m_gblang );
		m_sbuf1.safePrintf( "+gblang:%li |", m_gblang );
	}
	// bookmark here so we can copy into st->m_displayQuery below
	//long displayQueryOffset = m_sbuf1.length();
	// append url: term
	if ( m_urlLen > 0 ) {
		//if ( p > pstart ) *p++ = ' ';
		if ( m_sbuf1.length() ) m_sbuf1.pushChar(' ');
		//memcpy ( p , "+url:" , 5 ); p += 5;
		m_sbuf1.safeStrcpy ( "+url:");
		//memcpy ( p , m_url , m_urlLen ); p += m_urlLen;
		m_sbuf1.safeMemcpy ( m_url , m_urlLen );
	}
	// append url: term
	if ( m_linkLen > 0 ) {
		//if ( p > pstart ) *p++ = ' ';
		if ( m_sbuf1.length() ) m_sbuf1.pushChar(' ');
		//memcpy ( p , "+link:" , 6 ); p += 6;
		m_sbuf1.safeStrcpy ( "+link:");
		//memcpy ( p , m_link , m_linkLen ); p += m_linkLen;
		m_sbuf1.safeMemcpy ( m_link , m_linkLen );
	}
	// append the natural query
	if ( m_queryLen > 0 ) {
		//if ( p  > pstart  ) *p++  = ' ';
		if ( m_sbuf1.length() ) m_sbuf1.pushChar(' ');
		//p += ucToUtf8(p, pend-p, m_query, m_queryLen, csStr, 0,0);
		m_sbuf1.safeMemcpy ( m_query , m_queryLen );
		//memcpy ( p  , m_query , m_queryLen ); p  += m_queryLen;
		// add to spell checked buf, too		
		//if ( p2 > pstart2 ) *p2++ = ' ';
		if ( m_sbuf2.length() ) m_sbuf2.pushChar(' ');
		//p2 +=ucToUtf8(p2, pend2-p2, m_query, m_queryLen, csStr, 0,0);
		m_sbuf2.safeMemcpy ( m_query , m_queryLen );
		//memcpy ( p2 , m_query , m_queryLen ); p2 += m_queryLen;
	}
	if ( m_query2Len > 0 ) {
		//if ( p3 > pstart3 ) *p3++ = ' ';
		if ( m_sbuf3.length() ) m_sbuf3.pushChar(' ');
		//p3+=ucToUtf8(p3, pend3-p3, m_query2, m_query2Len, csStr,0,0);
		m_sbuf3.safeMemcpy ( m_query2 , m_query2Len );
	}
	//if (g_errno == EILSEQ){ // illegal character seq
	//	log("query: bad char set");
	//	g_errno = 0;
	//	if (qcs == csUTF8) {qcs = csISOLatin1;goto doOver;}
	//	if (qcs != csISOLatin1) {qcs = csUTF8;goto doOver;}
	//}
	// append quoted phrases to query
	if ( m_quoteLen1 > 0 ) {
		//if ( p  > pstart  ) *p++  = ' ';
		if ( m_sbuf1.length() ) m_sbuf1.pushChar(' ');
		//*p++ = '+';
		//*p++ = '\"';
		m_sbuf1.safeStrcpy("+\"");
		//p += ucToUtf8(p, pend-p, m_quote1, m_quoteLen1, csStr, 0,0);
		m_sbuf1.safeMemcpy ( m_quote1 , m_quoteLen1 );
		//memcpy ( p , m_quote1 , m_quoteLen1 ); p += m_quoteLen1 ;
		//*p++ = '\"';
		m_sbuf1.safeStrcpy("\"");
		// add to spell checked buf, too
		//if ( p2 > pstart2 ) *p2++ = ' ';
		if ( m_sbuf2.length() ) m_sbuf2.pushChar(' ');
		//*p2++ = '+';
		//*p2++ = '\"';
		m_sbuf2.safeStrcpy("+\"");
		//p2+=ucToUtf8(p2, pend2-p2, m_quote1, m_quoteLen1, csStr,0,0);
		m_sbuf2.safeMemcpy ( m_quote1 , m_quoteLen1 );
		//memcpy ( p2 , m_quote1 , m_quoteLen1 ); p2 += m_quoteLen1 ;
		//*p2++ = '\"';
		m_sbuf2.safeStrcpy("\"");
	}
	//if (g_errno == EILSEQ){ // illegal character seq
	//	g_errno = 0;
	//	if (qcs == csUTF8) {qcs = csISOLatin1;goto doOver;}
	//	if (qcs != csISOLatin1) {qcs = csUTF8;goto doOver;}
	//}
	if ( m_quoteLen2 > 0 ) {
		//if ( p  > pstart  ) *p++  = ' ';
		if ( m_sbuf1.length() ) m_sbuf1.pushChar(' ');
		//*p++ = '+';
		//*p++ = '\"';
		m_sbuf1.safeStrcpy("+\"");
		//p += ucToUtf8(p, pend-p, m_quote2, m_quoteLen2, csStr, 0,0);
		m_sbuf1.safeMemcpy ( m_quote2 , m_quoteLen2 );
		//memcpy ( p , m_quote2 , m_quoteLen2 ); p += m_quoteLen2 ;
		//*p++ = '\"';
		m_sbuf1.safeStrcpy("\"");
		// add to spell checked buf, too
		//if ( p2 > pstart2 ) *p2++ = ' ';
		if ( m_sbuf2.length() ) m_sbuf2.pushChar(' ');
		//*p2++ = '+';
		//*p2++ = '\"';
		m_sbuf2.safeStrcpy("+\"");
		//p2+=ucToUtf8(p2, pend2-p2, m_quote2, m_quoteLen2, csStr,0,0);
		m_sbuf2.safeMemcpy ( m_quote2 , m_quoteLen2 );
		//memcpy ( p2 , m_quote2 , m_quoteLen2 ); p2 += m_quoteLen2 ;
		//*p2++ = '\"';
		m_sbuf2.safeStrcpy("\"");
	}
	//if (g_errno == EILSEQ){ // illegal character seq
	//	g_errno = 0;
	//	if (qcs == csUTF8) {qcs = csISOLatin1;goto doOver;}
	//	if (qcs != csISOLatin1) {qcs = csUTF8;goto doOver;}
	//}
	
	// append plus terms
	if ( m_plusLen > 0 ) {
		char *s = m_plus, *send = m_plus + m_plusLen;
		//if ( p > pstart && p < pend ) *p++  = ' ';
		//if ( p2 > pstart2 && p2 < pend2) *p2++ = ' ';
		if ( m_sbuf1.length() ) m_sbuf1.pushChar(' ');
		if ( m_sbuf2.length() ) m_sbuf2.pushChar(' ');
		while (s < send) {
			while (isspace(*s) && s < send) s++;
			char *s2 = s+1;
			if (*s == '\"') {
				// if there's no closing quote just treat
				// the end of the line as such
				while (*s2 != '\"' && s2 < send) s2++;
				if (s2 < send) s2++;
			} else {
				while (!isspace(*s2) && s2 < send) s2++;
			}
			if (s < send) break;
			//if (p < pend) *p++ = '+';
			//if (p2 < pend2) *p2++ = '+';
			m_sbuf1.pushChar('+');
			m_sbuf2.pushChar('+');
			//p += ucToUtf8(p, pend-p, s, s2-s, csStr, 0,0);
			//p2 += ucToUtf8(p2, pend2-p2, s, s2-s, csStr, 0,0);
			m_sbuf1.safeMemcpy ( s , s2 - s );
			m_sbuf2.safeMemcpy ( s , s2 - s );
			/*
			if (g_errno == EILSEQ) { // illegal character seq
				g_errno = 0;
				if (qcs == csUTF8) {
					qcs = csISOLatin1;
					goto doOver;
				}
				if (qcs != csISOLatin1) {
					qcs = csUTF8;
					goto doOver;
				}
			}
			*/
			s = s2 + 1;
			if (s < send) {
				//if (p < pend) *p++ = ' ';
				//if (p2 < pend2) *p2++ = ' ';
				if ( m_sbuf1.length() ) m_sbuf1.pushChar(' ');
				if ( m_sbuf2.length() ) m_sbuf2.pushChar(' ');
			}
		}

	}  
	// append minus terms
	if ( m_minusLen > 0 ) {
		char *s = m_minus, *send = m_minus + m_minusLen;
		//if ( p > pstart && p < pend ) *p++  = ' ';
		//if ( p2 > pstart2 && p2 < pend2) *p2++ = ' ';
		if ( m_sbuf1.length() ) m_sbuf1.pushChar(' ');
		if ( m_sbuf2.length() ) m_sbuf2.pushChar(' ');
		while (s < send) {
			while (isspace(*s) && s < send) s++;
			char *s2 = s+1;
			if (*s == '\"') {
				// if there's no closing quote just treat
				// the end of the line as such
				while (*s2 != '\"' && s2 < send) s2++;
				if (s2 < send) s2++;
			} else {
				while (!isspace(*s2) && s2 < send) s2++;
			}
			if (s < send) break;
			//if (p < pend) *p++ = '-';
			//if (p2 < pend2) *p2++ = '-';
			m_sbuf1.pushChar('-');
			m_sbuf2.pushChar('-');
			//p += ucToUtf8(p, pend-p, s, s2-s, csStr, 0,0);
			//p2 += ucToUtf8(p2, pend2-p2, s, s2-s, csStr, 0,0);
			m_sbuf1.safeMemcpy ( s , s2 - s );
			m_sbuf2.safeMemcpy ( s , s2 - s );
			/*
			if (g_errno == EILSEQ) { // illegal character seq
				g_errno = 0;
				if (qcs == csUTF8) {
					qcs = csISOLatin1;
					goto doOver;
				}
				if (qcs != csISOLatin1) {
					qcs = csUTF8;
					goto doOver;
				}
			}
			*/
			s = s2 + 1;
			if (s < send) {
				//if (p < pend) *p++ = ' ';
				//if (p2 < pend2) *p2++ = ' ';
				if ( m_sbuf1.length() ) m_sbuf1.pushChar(' ');
				if ( m_sbuf2.length() ) m_sbuf2.pushChar(' ');
			}
		}
	}
	// append gbkeyword:numinlinks if they have &mininlinks=X, X>0
	long minInlinks = m_hr->getLong("mininlinks",0);
	if ( minInlinks > 0 ) {
		//if ( p > pstart ) *p++ = ' ';
		if ( m_sbuf1.length() ) m_sbuf1.pushChar(' ');
		//char *str = "gbkeyword:numinlinks";
		//long  len = gbstrlen(str);
		//memcpy ( p , str , len );
		//p += len;
		m_sbuf1.safePrintf ( "gbkeyword:numinlinks");
	}

	// null terms
	m_sbuf1.pushChar('\0');
	m_sbuf2.pushChar('\0');
	m_sbuf3.pushChar('\0');

	// the natural query
	m_displayQuery = m_sbuf2.getBufStart();// + displayQueryOffset;

	if ( ! m_displayQuery ) m_displayQuery = "";

	while ( *m_displayQuery == ' ' ) m_displayQuery++;

	m_displayQueryLen = gbstrlen(m_displayQuery);//p-m_displayQuery


	//log("query: got query %s",m_sbuf1.getBufStart());
	//log("query: got display query %s",m_displayQuery);

	// urlencoded display query
	urlEncode(m_qe,
		  MAX_QUERY_LEN*2,
		  m_displayQuery,
		  m_displayQueryLen);
	
	return true;
}

uint8_t SearchInput::detectQueryLanguage(void) {
	uint8_t lang = 0;
	// Check to see if default language is set.
	// This should override everything else.
	if(m_defaultSortLanguage)
			lang = getLanguageFromAbbr(m_defaultSortLanguage);

	// Set query language from User Agent string, if possible
	if(!lang && m_hr->getUserAgent())
		lang = g_langId.guessLanguageFromUserAgent(m_hr->getUserAgent());

	// guess from query terms
	if(!lang && m_q)
		lang = g_langId.guessLanguageFromQuery(m_q);

	// guess from IP addr of the requester
	if(!lang && m_queryIP)
		lang = g_langId.guessLanguageFromIP(m_queryIP);

	// Save for later
	m_langHint = lang;

	if(m_gbcountry && m_gbcountryLen > 0)
		m_country = g_countryCode.getIndexOfAbbr(m_gbcountry);

	if(!m_country) {
		// Now guess country of the query.
		char *codep = g_langId.findGeoIP(m_queryIP, geoIPNumRows - 1, 0);
		if(codep) m_country = g_countryCode.getIndexOfAbbr(codep);

		// Many doofuses just download firefox and don't set it
		// up properly, so this takes second place to the IP search.
		if(!m_country)
			m_country = g_langId.guessCountryFromUserAgent(m_hr->getUserAgent());

	}

	return(lang);
}
