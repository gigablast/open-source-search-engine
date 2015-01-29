#include "gb-include.h"

#include "CatRec.h"
//#include "SiteBonus.h"
#include "Lang.h"
//#include "DateParse.h"

//static int32_t getY(Xml *xml, int32_t n0,int32_t n1,int32_t X,
//		 char *strx,char *stry,int32_t def);

CatRec::CatRec (){
	reset();
};
CatRec::~CatRec() {}


void CatRec::reset() { 
	m_hadRec = false;
	//m_xml = NULL;
	m_catids = NULL;
	m_numCatids = 0; 
	m_numIndCatids = 0;
	m_dataSize = 0;
	//m_siteQuality = 0;
	//m_spamBits   = 0;
	//m_adultLevel = 0;
	//m_numTypes = 0;
	//m_numLangs = 0;
}

// . used by Msg8 to parse a serialized site rec into this CatRec class
// . we copy the info we need from "rec" so caller can free it
// . if rec is NULL or recSize is 0 we use the default xml 
// . returns false and sets g_errno on error
// . a CatRec has the format: (like a record in an RdbList)
// . kkkkkkkk kkkkkkkk kkkkkkkk kkkkkkkk  k = 96bit key (typical)
// . kkkkkkkk kkkkkkkk kkkkkkkk kkkkkkkk  
// . kkkkkkkk kkkkkkkk kkkkkkkk kkkkkkkk  
// . dddddddd dddddddd dddddddd dddddddd  d = dataSize of data below here
// .[nnnnnnnn cccccccc cccccccc cccccccc  n = number of catids, Catdb only
// . cccccccc cccccccc ........ ........] c = series of catids, int32_ts [Catdb]
// . ffffffff ffffffff ffffffff vvvvvvvv  v = version f = site fileNum (must be >= 0)
// . uuuuuuuu uuuuuuuu uuuuuuuu ........  u = var length site url
//   version >= 2:
// . ppssxxxx                             s = spam bits 
// .                                      p = adultLevel
// .                                      x = unused
//   version >= 3:
// .          qqqqqqqq                   q = site quality 



bool CatRec::set ( Url *url , char *data , int32_t dataSize , bool gotByIp ) {
	          //char rdbId ) {
	// assume url does not have a rec in tagdb
	m_hadRec = false;
	// set our collection
	//if ( coll ) gbmemcpy ( m_coll , coll , collLen );
	//m_collLen = collLen;
	// . if "data" is i guess the rec did not exist... so make a dummy rec
	// . MDW: why?
	if ( ! data || dataSize <= 0 ) {
		// default m_site to the hostname
		m_site.set (url->getHost(),url->getHostLen(),false/*addwww?*/);
		// steal ip from url
		m_site.setIp ( url->getIp() );
		// default xml for this collection
		//m_xml = g_tagdb.getSiteXml ( 0,/*filenum*/
		//			      coll, collLen); //, NULL , 0 );
		m_filenum = 0 ;
		//if ( m_xml ) return true;
		//g_errno = ENODATA;
		//return log("db: Could not find the ruleset file "
		//	   "%stagdb0.xml.",g_hostdb.m_dir);
		return true;
	}
	// return false and set g_errno if buf too small
	if ( dataSize >= CATREC_BUF_SIZE ) {
		g_errno = EBUFTOOSMALL;
		return false;
	}
	// copy the raw data
	gbmemcpy(m_data, data, dataSize);
	m_dataSize = dataSize;
	// set up a parsing ptr into "data"
	//char *p = data;
	char *p = m_data;
	// get the catids if using catdb
	//if (rdbId == RDB_CATDB) {
	m_numCatids = *(unsigned char*)p;
	p++;
	m_catids = (int32_t*)p;
	p += 4*m_numCatids;
	//}
	// point to the filenum so we can mod it!
	//m_filenumPtr = p;
	// get the filenum (0 is default)
	//m_filenum  = *(int32_t *) p ;  p += 4;
	m_filenum  = *(int32_t *) p ;  p += 3;
	// get the version
	if ( m_filenum == -1 ) {
		m_version = 0;
		p++;
	}
	else {
		m_filenum &= 0x00FFFFFF;
		m_version = *p;
		p++;
	}
	// calc site url length
	if ( m_version == 0 ) {
		m_urlLen = dataSize - 4;
		//if (rdbId == RDB_CATDB)
		m_urlLen -= (4*m_numCatids) + 1;
	}
	else
		m_urlLen = gbstrlen(p);
	// set our site url
	m_url = p;
	m_site.set ( p , m_urlLen , false/*addwww?*/);
	// move p to end of url
	p += m_urlLen;
	if ( m_version >= 1 )
		p++;
	// add time stamp, comment, username
	/*
	if ( m_version >= 2 && rdbId != RDB_CATDB ) {
		// time stamp
		m_timeStamp = *(int32_t*)p;
		p += 4;
		// comment
		m_comment = p;
		p += gbstrlen(m_comment) + 1;
		// username
		m_username = p;
		p += gbstrlen(m_username) + 1;
	}
	unsigned char siteFlags = 0;
	m_spamBits   = 0;
	m_adultLevel = 0;

	if ( m_version >= 3 && rdbId != RDB_CATDB ) {
		siteFlags = *p++;
		m_spamBits = siteFlags & 0xc0;  
	}

	//we've added a 1 byte quality and 2 bits for adult content level.
	if ( m_version >= 4 && rdbId != RDB_CATDB ) {
		m_siteQuality = *p++;
		m_adultLevel  = (siteFlags & 0x30);
	}

	m_incHere = NULL;
	m_addHere = NULL;

	if ( m_version >= 5 && rdbId != RDB_CATDB ) {

		// a marker for addSiteType() function below
		m_incHere = (int32_t *)p;

		m_numTypes = *(uint8_t*)p;
		p += sizeof(uint8_t);
			
		for(int32_t i = 0; i < m_numTypes; i++) {
			m_siteTypes[i].m_type = *(uint8_t*)p;
			p += sizeof(uint8_t);

			// version 6 adds 32-bit scores to site type
			if (m_version >= 6 &&
			    SiteType::isType4Bytes(m_siteTypes[i].m_type)) {
				m_siteTypes[i].m_score = *(uint32_t*)p;
				p += sizeof(uint32_t);
			}
			else {
				m_siteTypes[i].m_score = (uint32_t)*(uint8_t*)p;
				p += sizeof(uint8_t);
			}
		}

		// save ptr for addSiteTypes()
		m_addHere = p;

		//now for the languages
		m_numLangs = *(uint8_t*)p;
		p += sizeof(uint8_t);
			
		for(int32_t i = 0; i < m_numLangs; i++) {
			m_siteLangs[i].m_type = *(uint8_t*)p;
			p += sizeof(uint8_t);
			m_siteLangs[i].m_score = (uint32_t)*(uint8_t*)p;
			p += sizeof(uint8_t);
		}
	}
	*/

	// sanity check
	if ( p - m_data != m_dataSize ) {
		log ( "tagdb: Deserialized datasize %"INT32" != %"INT32" for url %s so "
		      "ignoring tagdb record.",
		      (int32_t)(p - m_data), m_dataSize , url->getUrl() );
		return false;
		char *xx = NULL; *xx = 0;
	}

	// if hostname is same as url we can use the ip from url
	if ( url && m_site.getHostLen() == url->getHostLen() )
		m_site.setIp ( url->getIp() );
	// . this url had it's own rec in the db
	// . Msg16 needs to know this so it won't auto-detect porn/spam in
	//   this url itself and delete it from tfndb
	m_hadRec = true;
	// if rec was in tagdb, data will be non-null.. did we get the rec
	// from tagdb by matching an IP? (as oppossed to canonical name)
	m_gotByIp = gotByIp;
	// get the xml for this filenum
	//m_xml = g_tagdb.getSiteXml ( m_filenum , coll , collLen );
	//if ( m_xml ) return true;
	// should NEVER be NULL
	//g_errno = ENODATA;
	//return log("db: Could not find the ruleset file %stagdb%"INT32".xml.",
	//	   g_hostdb.m_dir,m_filenum);
	return true;
}


bool CatRec::set ( Url *site , 
		   int32_t filenum ,
		   //char version , char rdbId ,
		   //int32_t timeStamp, char *comment , char *username ,
		    int32_t *catids , unsigned char numCatids
		   //unsigned char spamBits, char siteQuality,
		   //char adultLevel, 
		   //SiteType *siteTypes,
		   //uint8_t numTypes,
		   //SiteType *siteLangs,
		   //uint8_t numLangs) {
		   ) {
	// version
	m_version = CATREC_CURRENT_VERSION; // version;
	// how big should the site rec be?
	m_dataSize = 4 + site->getUrlLen() ;
	// null termination
	if ( CATREC_CURRENT_VERSION >= 1 )
		m_dataSize++;
	// add time stamp, comment, username
	//if ( version >= 2 && rdbId != RDB_CATDB ) {
	//	m_dataSize += 6;
	//	if (comment)
	//		m_dataSize += gbstrlen(comment);
	//	if (username)
	//		m_dataSize += gbstrlen(username);
	//}
	//the spam bits.
	//if ( version >= 3 && rdbId != RDB_CATDB) {
	//	m_dataSize++;
	//}
	//the site quality
	//if ( version >= 4 && rdbId != RDB_CATDB) {
	//	m_dataSize++;
	//}
	//if ( version >= 5 && rdbId != RDB_CATDB) {
	//	m_dataSize += sizeof(uint8_t);
	//	m_dataSize += numTypes * (sizeof(uint8_t) + sizeof(uint8_t));
	//	m_dataSize += sizeof(uint8_t);
	//	m_dataSize += numLangs * (sizeof(uint8_t) + sizeof(uint8_t));
	//}
	// . beginning with version 6, SiteType scores can be either 8-bit or
	//   32-bit, so add the extra bytes to the data size
	//if ( version >= 6 ) {
	//	for ( int32_t i = 0; i < numTypes; i++ ) {
	//		if ( SiteType::isType4Bytes(siteTypes[i].m_type) ) {
	//			m_dataSize += (sizeof(uint32_t) - 
	//				       sizeof(uint8_t));
	//		}
	//	}
	//}

	// sanity check
	if ( m_version > CATREC_CURRENT_VERSION ) {
		char *xx = NULL; *xx = 0; }
	// catids and numcatids
	//if (rdbId == RDB_CATDB)
	m_dataSize += 1 + (numCatids * 4);
	// return false and set g_errno if buf too small
	if ( m_dataSize > CATREC_BUF_SIZE ) {
		g_errno = EBUFTOOSMALL;
		return false;
	}
	// how about the actual dataSize?
	//m_dataSize = 4 + site->getUrlLen();
	// serialize into m_data
	char *p        = m_data;
	// get our key
	//key_t key = g_tagdb.makeKey (site, coll, collLen, false/*del?*/);
	//m_numTypes = numTypes;
	//sanity check:
	//if(m_numTypes > MAX_SITE_TYPES) {
	//	char *xx = NULL; *xx = 0;}

	// store numCatids and catids if exist
	m_numCatids = numCatids;
	if ( m_numCatids > MAX_CATIDS )
		m_numCatids = MAX_CATIDS;
	//if (catids) {
	//if (rdbId == RDB_CATDB) {
	// add the count
	gbmemcpy(p, &m_numCatids, 1);
	p++;
	// add the ids
	m_catids = (int32_t*)p;
	gbmemcpy(p, catids, 4*m_numCatids);
	// skip over "numCatids" NOT m_numCatids which is TRUNCATED
	// to MAX_CATIDS
	p += 4*numCatids;
	//}
	// point to the filenum so we can mod it!
	//m_filenumPtr = p;
	// store the filenum (3 bytes)
	//*(int32_t  *) p = filenum  ;   p += 4;
	//int32_t filenum = 0; // make this 0 for catdb rec: MDW
	gbmemcpy(p, &filenum, 3); p += 3;
	// store the version (1 byte)
	*p = m_version; p++;
	// the site
	m_url = p;
	m_urlLen = site->getUrlLen();
	gbmemcpy ( p , site->getUrl() , site->getUrlLen() );
	p += site->getUrlLen();
	// NULL terminate the site
	if ( m_version >= 1 ) {
		*p = '\0'; p++;
	}
	// add time stamp, comment, username
	/*
	if ( m_version >= 2 && rdbId != RDB_CATDB ) {
		// time stamp
		m_timeStamp = timeStamp;
		gbmemcpy(p, &timeStamp, 4);
		p += 4;
		// comment
		m_comment = p;
		if (comment) {
			strcpy(p, comment);
			p += gbstrlen(comment) + 1;
		}
		else {
			*p = '\0';
			p++;
		}
		// username
		m_username = p;
		if (username) {
			strcpy(p, username);
			p += gbstrlen(username) + 1;
		}
		else {
			*p = '\0';
			p++;
		}
	}
	m_adultLevel = adultLevel;
	m_spamBits = spamBits;
	unsigned char siteFlags = 0;
	siteFlags |= m_adultLevel;
	siteFlags |= m_spamBits;
	
	if ( m_version >= 3 && rdbId != RDB_CATDB ) {
		*p = siteFlags;
		p++;
	}
	if ( m_version >= 4 && rdbId != RDB_CATDB ) {
		*p = siteQuality;
		p++;
	}

	// reset this
	m_addHere = NULL;
	m_incHere = NULL;

	if ( m_version >= 5 && rdbId != RDB_CATDB ) {

		// a marker for addSiteType() function below
		m_incHere = (int32_t *)p;

		*(uint8_t*)p = numTypes;
		p += sizeof(uint8_t);
		for(int32_t i = 0; i < numTypes; i++) {
			*(uint8_t*)p = siteTypes[i].m_type;
			p += sizeof(uint8_t);

			// version 6 adds 32-bit scores to site type
			if ( m_version >= 6 && 
			     SiteType::isType4Bytes( siteTypes[i].m_type ) ) {
				*(uint32_t*)p = siteTypes[i].m_score; 
				p += sizeof(uint32_t);
			}
			else {
				*(uint8_t*)p = (uint8_t)siteTypes[i].m_score;
				p += sizeof(uint8_t);
			}
		}
		// this is a marker where to add site types from
		// addSiteType() function below
		m_addHere = p;
		*(uint8_t*)p = numLangs;
		p += sizeof(uint8_t);
		for(int32_t i = 0; i < numLangs; i++) {
			*(uint8_t*)p = siteLangs[i].m_type;
			p += sizeof(uint8_t);
			*(uint8_t*)p = siteLangs[i].m_score;
			p += sizeof(uint8_t);
		}
	}
	*/

	// sanity check
	if ( p - m_data != m_dataSize ) {
		log ( "catrec: Serialized datasize %"INT32" != %"INT32"",
		      (int32_t)(p - m_data), (int32_t)m_dataSize );
		char *xx = NULL; *xx = 0;
	}
	// set our member vars correctly in addition to the site rec
	m_site.set ( site->getUrl(), site->getUrlLen(), false/*addwww?*/);
	// steal ip from "site"
	m_site.setIp ( site->getIp() );
	// save the collection into m_coll
	//gbmemcpy ( m_coll , coll , collLen );
	//m_collLen = collLen;
	// save the fileNum as well
	//m_filenum = filenum;
	// make sure xml is set
	//m_xml = g_tagdb.getSiteXml ( m_filenum , coll , collLen );
	//if ( m_xml ) return true;	
	// should NEVER be NULL
	//g_errno = ENODATA;
	//return log("db: Could not find the ruleset file %stagdb%"INT32".xml.",
	//	   g_hostdb.m_dir,m_filenum);
	return true;
}


// keep everything else the same
/*
bool CatRec::set ( int32_t filenum ) {
	// save the fileNum
	m_filenum = filenum;
	// make sure xml is set
	m_xml = g_tagdb.getSiteXml ( m_filenum , m_coll , m_collLen );
	if ( m_xml ) return true;	
	// should NEVER be NULL
	g_errno = ENODATA;
	return log("db: Could not find the ruleset file %stagdb%"INT32".xml.",
		   g_hostdb.m_dir,m_filenum);
}
*/

// . this set method just sets the site records filenum, version,
//   url and url len
// . this method is added to skip the getSiteXml and other
//   overheads
bool CatRec::set ( char *data, int32_t dataSize ) {//, char rdbId ){
	
	
	if ( !data || dataSize <= 0 )
		return false;


	//if (rdbId == RDB_CATDB) {
	m_numCatids = *(unsigned char*)data;
	data++;
	m_catids = (int32_t*)data;
	data += 4*m_numCatids;
	//}
	// get the filenum (0 is default)
	//m_filenum  = *(int32_t *) p ;  p += 4;
	m_filenum  = *(int32_t *) data ;  data += 3;

	// get the version
	if ( m_filenum == -1 ) {
		m_version = 0;
		data++;
	}
	else {
		m_filenum &= 0x00FFFFFF;
		m_version = *data;
		data++;
	}

	// calc site url length
	if ( m_version == 0 ) {
		m_urlLen = dataSize - 4;
		//if (rdbId == RDB_CATDB)
		m_urlLen -= (4*m_numCatids) + 1;
	}
	else
		m_urlLen = gbstrlen(data);
	// set our site url
	m_url = data;
	m_site.set ( data , m_urlLen , false);

	return true;
}


// set the indirect catids
void CatRec::setIndirectCatids ( int32_t *indCatids, int32_t numIndCatids ) {
	// store the number of ids
	m_numIndCatids = numIndCatids;
	if ( m_numIndCatids > MAX_IND_CATIDS )
		m_numIndCatids = MAX_IND_CATIDS;
	// store the ids
	gbmemcpy ( m_indCatids, indCatids, m_numIndCatids*4 );
}

/*
int32_t CatRec::getMaxLenFromQuality      ( int32_t n0, int32_t n1, int32_t quality ) {
	return getY (n0,n1, quality, "index.quality1","index.maxLen1",64000);}
int32_t CatRec::getMaxScoreFromQuality    ( int32_t n0, int32_t n1, int32_t quality ) {
	int32_t max=getY (n0,n1,quality,"index.quality2","index.maxScore2",100);
	if ( max > 100 ) {
		log("db: Encountered maxScore from quality > 100 in ruleset "
		    "file. Truncating to 100.");
		max = 100;
	}
	return max;
}
//bool CatRec::hasMaxCountFromQualityTag ( int32_t n0, int32_t n1 ) {
//	int32_t max=getY (n0,n1,50,"index.quality4","index.maxCount4",-9321);
//	if ( max == -9321 ) return false;
//	return true;
//}
//
//int32_t CatRec::getMaxCountFromQuality    ( int32_t n0, int32_t n1, int32_t quality ) {
//	// 100 in this sense is not a percentage, but an actual word count
//	int32_t max=getY (n0,n1,quality,"index.quality4","index.maxCount4",
//		       9999999);
//	if ( max < 0 ) {
//		log("db: Encountered maxScore from quality of %"INT32" in ruleset "
//		    "file. Setting to 0.",max);
//		max = 0;
//	}
//	return max;
//}
int32_t CatRec::getScoreWeightFromQuality ( int32_t n0, int32_t n1, int32_t quality ) {
	return getY (n0,n1,quality,"index.quality3","index.scoreWeight3",100);}
int32_t CatRec::getScoreWeightFromQuality2( int32_t quality ) {
	return getY (0,999999,quality,"quality3","scoreWeight3",100);}
int32_t CatRec::getScoreWeightFromLen     ( int32_t n0, int32_t n1, int32_t len     ) {
	return getY (n0,n1, len    , "index.len4" ,"index.scoreWeight4",100);}
int32_t CatRec::getScoreWeightFromLen2    ( int32_t len     ) {
	return getY (0,999999, len    , "len4" ,"scoreWeight4",100);}
int32_t CatRec::getScoreWeightFromNumWords( int32_t n0, int32_t n1, int32_t len     ) {
	return getY (n0,n1, len , "index.numWords6","index.scoreWeight6",100);}
int32_t CatRec::getMaxScoreFromLen        ( int32_t n0, int32_t n1, int32_t len     ) {
	int32_t max = getY (n0,n1, len, "index.len5"    ,"index.maxScore5",100);
	if ( max > 100 ) {
		log("db: Encountered maxScore from length > 100 in ruleset "
		    "file. Truncating to 100.");
		max = 100;
	}
	return max;
}
int32_t CatRec::getMaxScoreFromNumWords     ( int32_t n0, int32_t n1, int32_t len     ) {
	int32_t max = getY (n0,n1, len, "index.numWords7","index.maxScore7",100);
	if ( max > 100 ) {
		log("db: Encountered maxScore from length > 100 in ruleset "
		    "file. Truncating to 100.");
		max = 100;
	}
	return max;
}
int32_t CatRec::getQualityBoostFromNumLinks       ( int32_t numLinks    ) {
	return getY (0,99999, numLinks,"numLinks1"  ,"qualityBoost1",100); }
int32_t CatRec::getQualityBoostFromLinkQualitySum ( int32_t sum         ) {
	return getY (0,99999, sum    ,"linkQualitySum2","qualityBoost2",100);}
int32_t CatRec::getQualityBoostFromRootQuality    ( int32_t rootQuality ) {
	return getY (0,99999,rootQuality,"rootQuality3","qualityBoost3",100); }

int32_t CatRec::getLinkTextScoreWeightFromLinkerQuality ( int32_t quality     ) {
	return getY (0,99999,quality  ,"quality4","linkTextScoreWeight4",100);}
int32_t          getLinkTextScoreWeightFromLinkerQuality ( Xml *xml , int32_t quality ) {
	return getY (xml,0,99999,quality  ,"quality4","linkTextScoreWeight4",100);}
int32_t CatRec::getLinkTextScoreWeightFromLinkeeQuality ( int32_t quality     ) {
	return getY (0,99999,quality  ,"quality7","linkTextScoreWeight7",100);}
int32_t          getLinkTextScoreWeightFromLinkeeQuality ( Xml *xml , int32_t quality ) {
	return getY (xml,0,99999,quality  ,"quality7","linkTextScoreWeight7",100);}

int32_t CatRec::getLinkTextScoreWeightFromNumWords( int32_t numWords ) {
	return getY (0,99999,numWords  ,"linkTextNumWords6",
		     "linkTextScoreWeight6", 100); }
int32_t CatRec::getQuotaBoostFromRootQuality      ( int32_t rootQuality ) {
	return getY (0,99999,rootQuality,"rootQuality7","quotaBoost7",100); }
int32_t CatRec::getQuotaBoostFromQuality          ( int32_t quality ) {
	return getY (0,99999,quality,"quality8","quotaBoost8",100); }
int32_t CatRec::getLinkTextMaxScoreFromQuality    ( int32_t quality     ) {
	int32_t max = getY(0,99999,quality,"quality5","linkTextMaxScore5",100); 
	if ( max > 100 ) {
		log("db: Encountered linkText maxScore from quality > 100 in "
		    "ruleset file. Truncating to 100.");
		max = 100;
	}
	return max;
}
int32_t CatRec::getMaxPercentForSpamFromQuality ( int32_t quality ) {
	// old ruleset files (tagdb*.xml) do not have this, so it *has* to
	// default to 4 to preserve the old method... so we can properly
	// delete docs.
	int32_t max = getY(0,99999,quality,"quality6","maxPercentSpammed6",4); 
	// a safety catch
	if ( max < 4 ) {
		max = 4;
		static char s_flag = 0;
		if ( s_flag == 0 ) {
			log("db: Encountered max percent threshold for spam "
			    "that is less than 4. Setting to 4. This message "
			    "will not be repeated.");
			s_flag = 1;
		}
	}
	return max;
}

// . grab the Y value given the X
// . assumes a graph like:
//  <index>
//    <quality21>                 %uc </> 
//    <quality22>                 %uc </> 
//    <quality23>                 %uc </> 
//    <quality24>                 %uc </> 
//    <quality25>                 %uc </> 
//    <maxScore21>                %ul </> 
//    <maxScore22>                %ul </> 
//    <maxScore23>                %ul </> 
//    <maxScore24>                %ul </> 
//    <maxScore25>                %ul </> 
//  </index>
// . where strx = "index.quality2" 
// .       stry = "index.maxScore2"
// . this example maps a quality to a maxScore
int32_t CatRec::getY(int32_t n0,int32_t n1,int32_t X,char *strx,char *stry,int32_t def){
	return ::getY(m_xml,n0,n1,X,strx,stry,def);
}

int32_t getY(Xml *xml, int32_t n0,int32_t n1,int32_t X,char *strx,char *stry,int32_t def){
	// . make the name buffers
	// . generates labels for the (x,y) points
	// . we can have up to 32 points
	char buf[64];
	int32_t x[32], y[32];
	int32_t i;
	for ( i = 0 ; i < 32 ; i++ ) {
		// get the x value (i.e. "quality23")
		sprintf ( buf, "%s%"INT32"", strx , i+1 );
		x[i] = xml->getLong ( n0, n1, buf , -1 );
		// break if this x point ain't present
		if ( x[i] == -1 ) break;
		// get the y value (i.e. "maxScore23")
		sprintf ( buf, "%s%"INT32"", stry , i+1 );
		y[i] = xml->getLong ( n0, n1, buf , -1 );
		// break if this y point ain't present
		if ( y[i] == -1 ) break;
	}
	// n is our number of (x,y) points
	int32_t n = i;
	// bitch if no points present and return 0
	if ( n == 0 ) {
		static char s_flag = 0;
		// only print out once if it is the quality6/maxPercentSpammed
		// map because that is a new thing
		if ( s_flag == 1 && strx && ! strcmp ( "quality6" , strx ) ) 
			return def;
		// ok, there's other missing things, too
		if ( s_flag == 1 ) 
			return def;
		s_flag = 1;
		log("db: No map present in a ruleset file (tagdb*.xml) for "
		    "%s/%s. Using default of %"INT32".",strx,stry,def);
		return def;
	}
	// if we only have one point then there'll be no interpolation
	if ( n == 1 ) return y[0];
	// find the first x after our "X"
	int32_t j;
	for ( j = 0 ; j < n; j++ ) if ( x[j] >= X ) break;
	// before/after first/last point means we don't have to interpolate
	if ( j <= 0 ) return y[0  ];
	if ( j >= n ) return y[n-1];
	// linear interpolate between our 2 points (x0,y0) and (x1,y1)
	int64_t x0 = x[j-1];
	int64_t x1 = x[j  ];
	int64_t y0 = y[j-1];
	int64_t y1 = y[j  ];
	// error if x1 less than x0
	if ( x1 <= x0 ) {
		log("db: X coordinates are not in ascending order for map "
		    "(%s,%s) in a ruleset file (tagdb*.xml).",strx,stry);
		return def;
	}
	// otherwise we have a sloping line
	return  y0 + ( ((int64_t)X - x0) * (y1-y0) ) /(x1-x0) ;
}


void CatRec::printFormattedRec(SafeBuf *sb) {
	
	struct tm *timeStruct = localtime ( &m_timeStamp );
	char tbuf[64];
	strftime ( tbuf, 64 , "%b-%d-%Y(%H:%M:%S) ", timeStruct );

	sb->safePrintf("<tr><td>Site:            </td><td>%s</td></tr>\n"
		       "<tr><td>Site File Number:</td><td>%"INT32"</td></tr>\n"
		       "<tr><td>Had Rec:         </td><td>%s</td></tr>\n"
		       "<tr><td>Version:         </td><td>%"INT32"</td></tr>\n"
		       "<tr><td>Timestamp:       </td><td>%s</td></tr>\n"
		       "<tr><td>Comment:         </td><td>%s</td></tr>\n"
		       "<tr><td>Username:        </td><td>%s</td></tr>\n"
		       "<tr><td>Site Quality:    </td><td>%"INT32"</td></tr>\n"
		       "<tr><td>Spam Status:     </td><td>%s</td></tr>\n"
		       "<tr><td>Adult Level:     </td><td>%s</td></tr>\n"
		       "<tr><td>Alexa Rank:      </td><td>%"INT32"</td></tr>\n",
		       m_site.getUrl(),
		       (int32_t)m_filenum,
		       m_hadRec?"YES":"NO",
		       (int32_t)m_version,
		       tbuf,//m_timeStamp,
		       m_comment,
		       m_username,
		       (int32_t)m_siteQuality,
		       getSpamStr(),
		       getAdultStr(),
		       g_siteBonus.getAlexaRanking(&m_site));

	for(int32_t i = 0;i < m_numTypes; i++) {
		sb->safePrintf("<tr><td>%s:</td><td>%"INT32"</td></tr>\n",
			       SiteType::getSiteTypeStr(m_siteTypes[i].m_type),
			       (int32_t)m_siteTypes[i].m_score);
	}
	for(int32_t i = 0;i < m_numLangs; i++) {
		sb->safePrintf("<tr><td>%s:</td><td>%"INT32"</td></tr>\n",
			       getLanguageString(m_siteLangs[i].m_type),
			       (int32_t)m_siteLangs[i].m_score);
	}
}

char* CatRec::printFormattedRec(char* p) {
	p += sprintf(p, 
		     "<tr><td>Site:            </td><td>%s</td></tr>\n"
		     "<tr><td>Site File Number:</td><td>%"INT32"</td></tr>\n"
		     "<tr><td>Had Rec:         </td><td>%s</td></tr>\n"
		     "<tr><td>Version:         </td><td>%"INT32"</td></tr>\n"
		     "<tr><td>Timestamp:       </td><td>%"INT32"</td></tr>\n"
		     "<tr><td>Comment:         </td><td>%s</td></tr>\n"
		     "<tr><td>Username:        </td><td>%s</td></tr>\n"
		     "<tr><td>Spam Status:     </td><td>%s</td></tr>\n"
		     "<tr><td>Adult Level:     </td><td>%s</td></tr>\n"
		     "<tr><td>Alexa Rank:      </td><td>%"INT32"</td></tr>\n",
		     m_site.getUrl(),
		     (int32_t)m_filenum,
		     m_hadRec?"YES":"NO",
		     (int32_t)m_version,
		     m_timeStamp,
		     m_comment,
		     m_username,
		     getSpamStr(),
		     getAdultStr(),
		     g_siteBonus.getAlexaRanking(&m_site));

	for(int32_t i = 0;i < m_numTypes; i++) {
		p += sprintf(p, 
			     "<tr><td>%s:</td><td>%"INT32"</td></tr>\n",
			     SiteType::getSiteTypeStr(m_siteTypes[i].m_type),
			     (int32_t)m_siteTypes[i].m_score);
	}

	for(int32_t i = 0;i < m_numLangs; i++) {
		p += sprintf("<tr><td>%s:</td><td>%"INT32"</td></tr>\n",
			     getLanguageString(m_siteLangs[i].m_type),
			     (int32_t)m_siteLangs[i].m_score);
	}

	return p;
}


uint32_t CatRec::getScoreForType(uint8_t type) {
	for(int32_t i = 0; i < m_numTypes; i++) {
		if(m_siteTypes[i].m_type == type) return m_siteTypes[i].m_score;
	}
	return 0;
}

void CatRec::setFilenum ( int32_t filenum ) {
	m_filenum = filenum;
	// gotta update the m_data[] buffer too!
	gbmemcpy(m_filenumPtr, &filenum, 3); 
}

void CatRec::addSiteType ( uint8_t type, uint32_t score ) {
	if ( m_numTypes >= MAX_SITE_TYPES ) {
		log("build: hit max site types!");return;}
	// this is NOT supported for older version that had no site types!
	if ( m_version < 5 ) return;
	// score of 0 means none i guess? a reserved value!
	if ( score == 0 ) {
		log("build: adding site type with zero score!");
		char *xx = NULL; *xx = 0; 
	}
	m_siteTypes[m_numTypes].m_type  = type;
	m_siteTypes[m_numTypes].m_score = score;
	m_numTypes++;
	// the type size!
	int32_t scoreSize = SiteType::getScoreSize(type);
	// size of site type and score combined
	int32_t totalSize = 1 + scoreSize;
	// shift the data in m_data!
	char *p = m_addHere;
	// how much to shift down 
	int32_t toShift = m_data + m_dataSize - p;
	// shift it
	gbmemcpy ( p + totalSize , p , toShift );
	// store new type
	*(uint8_t *)p = type; p++;
	// store new score
	gbmemcpy ( p , &score , scoreSize ); 
	// inc data size
	m_dataSize += totalSize;
	// inc this guy
	m_addHere += totalSize;
	// inc this too!
	*m_incHere = *m_incHere + 1;
}

char* CatRec::printXmlRec(char* p) {
	p += sprintf(p, 
		     "\t<site><![CDATA[%s]]></site>\n"
		     "\t<siteFileNumber>%"INT32"</siteFileNumber>\n"
		     "\t<hadRec><![CDATA[%s]]></hadRec>\n"
		     "\t<version>%"INT32"</version>\n"
		     "\t<timestamp>%"INT32"</timestamp>\n"
		     "\t<comment><![CDATA[%s]]></comment>\n"
		     "\t<username><![CDATA[%s]]></username>\n"
		     "\t<siteQuality>%"INT32"</siteQuality>\n"
		     "\t<spamStatus><![CDATA[%s]]></spamStatus>\n"
		     "\t<adultLevel><![CDATA[%s]]></adultLevel>\n"
		     "\t<alexaRank>%"INT32"</alexaRank>\n"
		     "\t<banned>%i</banned>\n",
		     m_site.getUrl(),
		     (int32_t)m_filenum,
		     m_hadRec?"YES":"NO",
		     (int32_t)m_version,
		     m_timeStamp,
		     m_comment,
		     m_username,
		     (int32_t)m_siteQuality,
		     getSpamStr(),
		     getAdultStr(),
		     g_siteBonus.getAlexaRanking(&m_site),
		     m_xml->getBool("isBanned", false));
	return p;
}

void CatRec::printXmlRec( SafeBuf *sb ) {
	sb->safePrintf("\t<site><![CDATA[%s]]></site>\n"
		       "\t<siteFileNumber>%"INT32"</siteFileNumber>\n"
                       "\t<hadRec><![CDATA[%s]]></hadRec>\n"
                       "\t<version>%"INT32"</version>\n"
                       "\t<timestamp>%"INT32"</timestamp>\n"
                       "\t<comment><![CDATA[%s]]></comment>\n"
                       "\t<username><![CDATA[%s]]></username>\n"
                       "\t<siteQuality>%"INT32"</siteQuality>\n"
                       "\t<spamStatus><![CDATA[%s]]></spamStatus>\n"
                       "\t<adultLevel><![CDATA[%s]]></adultLevel>\n"
                       "\t<alexaRank>%"INT32"</alexaRank>\n"
                       "\t<banned>%i</banned>\n",
                       m_site.getUrl(),
                       (int32_t)m_filenum,
                       m_hadRec?"YES":"NO",
                       (int32_t)m_version,
                       m_timeStamp,
                       m_comment,
                       m_username,
                       (int32_t)m_siteQuality,
                       getSpamStr(),
                       getAdultStr(),
                       g_siteBonus.getAlexaRanking(&m_site),
                       m_xml->getBool("isBanned", false));
}


char* CatRec::getSpamStr() {
	if ( m_version >= 3 ) {
		switch (m_spamBits) {
		case SPAM_BIT:
			return "spam";
		case NOT_SPAM:
			return "not spam"; break;
		case SPAM_UNKNOWN:
			return "unknown";  break;
		default:
			return "corrupt";  break;
		}
	}
	return "unknown"; 
}


char* CatRec::getAdultStr() {
	if ( m_version >= 4 ) {
		switch (m_adultLevel) {
		case RATED_G:
			return "kid safe";
		case RATED_R:
			return "adult, not porn"; break;
		case RATED_X:
			return "porn";  break;
		default:
			return "not rated";  break;
		}
	}
	return "not rated"; 
	

}


char *CatRec::getPubDateFmtStr() {
	int32_t fmt = getScoreForType(SiteType::DATE_FORMAT);
	switch (fmt) {
	case DateParse::DATE_FMT_AMER:
		return "American";
	case DateParse::DATE_FMT_EURO:
		return "European";
	}
	return "Unknown/Ambiguous";
}
*/
