#include "Images.h"
#include "Query.h"
#include "Xml.h"
#include "Words.h"
#include "Sections.h"
#include "XmlDoc.h"
#include "Threads.h"
#include "Hostdb.h"
#include "XmlDoc.h" // my_system_r()

// TODO: image is bad if repeated on same page, check for that

//static void gotTermFreqWrapper ( void *state ) ;
static void gotTermListWrapper ( void *state ) ;
static void *thumbStartWrapper_r ( void *state , ThreadEntry *te );
static void getImageInfo ( char *buf, long size, long *dx, long *dy, long *it);

Images::Images ( ) {
	reset();
}

void Images::reset() {
	m_imgData        = NULL;
	m_imgDataSize    = 0;
	m_setCalled      = false;
	m_thumbnailValid = false;
	m_imgReply       = NULL;
	m_imgReplyLen    = 0;
	m_imgReplyMaxLen = 0;
	m_numImages      = 0;
	m_imageBufValid  = false;
	m_phase = 0;
}

/*
bool Images::hash ( long       titleRecVersion ,
		    Xml       *xml             ,
		    Url       *pageUrl         ,
		    TermTable *table           ,
		    long       score           ) {

	for ( long i = 0 ; i < m_numImages ; i++ ) {
		// get the node number
		long nn = m_imageNodes[i];
		// get the url of the image
		long  srcLen;
		char *src = xml->getString(nn,nn+1,"src",&srcLen);
		// set it to the full url
		Url iu;
		// use "pageUrl" as the baseUrl
		iu.set ( pageUrl , src , srcLen , true );  // addWWW? yes...
		// hash each one
		if ( ! table->hash ( titleRecVersion ,
				     "image"      , // desc
				     5            , // descLen
				     "gbimage"    , // field
				     7            ,
				     iu.getUrl(),
				     iu.getUrlLen(),
				     score*256,
				     TERMTABLE_MAXSCORE , // maxScore
				     false , // doSpamDetection?
				     false , // hashSingletons?
				     false , // hashPhrases?
				     true  , // hashAsWhole?
				     false , // useStems?
				     false , // useStopWords?
				     true  , // hashIfUniqueOnly?
				     false ))// hashSingletonsIffNotInPhrase
			return false;
	}
	return true;
}
*/

void Images::setCandidates ( Url *pageUrl , Words *words , Xml *xml ,
			     Sections *sections , XmlDoc *xd ) {
	// not valid for now
	m_thumbnailValid = false;
	// reset our array of image node candidates
	m_numImages = 0;
	// flag it
	m_setCalled = true;
	// strange...
	if ( m_imgReply ) { char *xx=NULL;*xx=0; }
	// save this
	m_xml       = xml;
	m_pageUrl   = pageUrl;

	// if we are a diffbot json reply, trust that diffbot got the
	// best candidate, and just use that
	if ( xd->m_isDiffbotJSONObject ) return;

	//m_pageSite  = pageSite;
	// scan the words
	long       nw     = words->getNumWords();
	nodeid_t  *tids   = words->getTagIds();
	long long *wids   = words->getWordIds();
	//long      *scores = scoresArg->m_scores;
	Section **sp = NULL; 
	if ( sections ) sp = sections->m_sectionPtrs;
	// not if we don't have any identified sections
	if ( sections && sections->m_numSections <= 0 ) sp = NULL;
	// the positive scored window
	long firstPosScore = -1;
	long lastPosScore  = -1;
	long badFlags = SEC_SCRIPT|SEC_STYLE|SEC_SELECT|SEC_MARQUEE;
	// find positive scoring window
	for ( long i = 0 ; i < nw ; i++ ) {
		// skip if in bad section
		if ( sp && (sp[i]->m_flags & badFlags) ) continue;
		if ( wids[i]   != 0 ) continue;
		// set first positive scoring guy
		if ( firstPosScore == -1 ) firstPosScore = i;
		// keep track of last guy
		lastPosScore = i;
	}
	// sanity check
	if ( getNumXmlNodes() > 512 ) { char *xx=NULL;*xx=0; }
	// . pedal firstPosScore back until we hit a section boundary
	// . i.e. stop once we hit a front/back tag pair, like <div> and </div>
	char tc[512];
	memset ( tc , 0 , 512 );
	long a = firstPosScore;
	for ( ; a >= 0 ; a-- ) {
		// get the tid
		nodeid_t tid = tids[a];
		// remove back bit, if any
		tid &= BACKBITCOMP;
		// skip if not a tag, or a generic xml tag
		if ( tid <= 1 ) continue;
		// mark it
		if ( words->isBackTag(a) ) tc[tid] |= 0x02;
		else                       tc[tid] |= 0x01;
		// continue if not a full front/back pair
		if ( tc[tid] != 0x03 ) continue;
		// continue if not a "section" type tag (see Scores.cpp)
		if ( tid != TAG_DIV      &&
		     tid != TAG_TEXTAREA &&
                     tid != TAG_TR       &&
                     tid != TAG_TD       &&
                     tid != TAG_TABLE      ) 
			continue;
		// ok we should stop now
		break;
	}		
	// min is 0
	if ( a < 0 ) a = 0;

	// now look for the image urls within this window
	for ( long i = a ; i < lastPosScore ; i++ ) {
		// skip if not <img> tag
		if (tids[i] != TAG_IMG ) continue;
		// get the node num into Xml.cpp::m_nodes[] array
		long nn = words->m_nodes[i];
		// check width to rule out small decorating imgs
		long width = xml->getLong(nn,nn+1,"width", -1 );
		if ( width != -1 && width < 50 ) continue;
		// same with height
		long height = xml->getLong(nn,nn+1, "height", -1 );
		if ( height != -1 && height < 50 ) continue;
		// get the url of the image
		long  srcLen;
		char *src = xml->getString(nn,"src",&srcLen);
		// skip if none
		if ( srcLen <= 2 ) continue;
		// set it to the full url
		Url iu;
		// use "pageUrl" as the baseUrl
		iu.set ( pageUrl , src , srcLen ); 
		// skip if invalid domain or TLD
		if ( iu.getDomainLen() <= 0 ) continue;
		// skip if not from same domain as page url
		//long dlen = pageUrl->getDomainLen();
		//if ( iu.getDomainLen() != dlen ) continue;
		//if(strncmp(iu.getDomain(),pageUrl->getDomain(),dlen))continue
		// get the full url
		char *u    = iu.getUrl();
		long  ulen = iu.getUrlLen();
		// skip common crap
		if ( strncasestr(u,ulen,"logo"           ) ) continue;
		if ( strncasestr(u,ulen,"comment"        ) ) continue;
		if ( strncasestr(u,ulen,"print"          ) ) continue;
		if ( strncasestr(u,ulen,"subscribe"      ) ) continue;
		if ( strncasestr(u,ulen,"header"         ) ) continue;
		if ( strncasestr(u,ulen,"footer"         ) ) continue;
		if ( strncasestr(u,ulen,"menu"           ) ) continue;
		if ( strncasestr(u,ulen,"button"         ) ) continue;
		if ( strncasestr(u,ulen,"banner"         ) ) continue;
		if ( strncasestr(u,ulen,"ad.doubleclick.") ) continue;
		if ( strncasestr(u,ulen,"ads.webfeat."   ) ) continue;
		if ( strncasestr(u,ulen,"xads.zedo."     ) ) continue;

		// save it
		m_imageNodes[m_numImages] = nn;

		// before we lookup the image url to see if it is unique we
		// must first make sure that we have an adequate number of
		// permalinks from this same site with this same hop count.
		// we need at least 10 before we extract image thumbnails.
		char buf[2000];
		// set the query
		Query q;

		// if we do have 10 or more, then we lookup the image url to
		// make sure it is indeed unique
		sprintf ( buf , "gbimage:%s",u);
		// TODO: make sure this is a no-split termid storage thingy
		// in Msg14.cpp
		if ( ! q.set2 ( buf , langUnknown , false ) )
			// return true with g_errno set on error
			return;
		// store the termid
		m_termIds[m_numImages] = q.getTermId(0);

		// advance the counter
		m_numImages++;

		// break if full
		if ( m_numImages >= MAX_IMAGES ) break;
	}
}

// . returns false if blocked, returns true otherwise
// . sets g_errno on error
bool Images::getThumbnail ( char *pageSite ,
			    long  siteLen  ,
			    long long docId ,
			    XmlDoc *xd ,
			    collnum_t collnum,//char *coll ,
			    //char **statusPtr ,
			    long hopCount,
			    void *state ,
			    void   (*callback)(void *state) ) {
	// sanity check
	if ( ! m_setCalled ) { char *xx=NULL;*xx=0; }
	// we haven't had any error
	m_hadError  = 0;
	// no reason to stop yet
	m_stopDownloading = false;
	// reset here now
	m_i = 0;
	m_j = 0;
	m_phase = 0;

	// sanity check
	if ( ! m_pageUrl ) { char *xx=NULL;*xx=0; }
	// sanity check
	if ( ! pageSite ) { char *xx=NULL;*xx=0; }
	// we need to be a permalink
	//if ( ! isPermalink ) return true;

	// save these
	//m_statusPtr = statusPtr;
	// save this
	m_collnum = collnum;
	m_docId = docId;
	m_callback = callback;
	m_state = state;

	// if this doc is a json diffbot reply it already has the primary
	// image selected so just use that
	m_xd = xd;
	if ( m_xd->m_isDiffbotJSONObject ) 
		return downloadImages();

	// if no candidates, we are done, no error
	if ( m_numImages == 0 ) return true;

	//Vector *v = xd->getTagVector();
	// this will at least have one component, the 0/NULL component
	uint32_t *tph = xd->getTagPairHash32();
	// must not block or error on us
	if ( tph == (void *)-1 ) { char *xx=NULL;*xx=0; }
	// must not error on use?
	if ( ! tph ) { char *xx=NULL;*xx=0; }

	// . see DupDetector.cpp, very similar to this
	// . see how many pages we have from our same site with our same 
	//   html template (and that are permalinks)
	char buf[2000];
	char c = pageSite[siteLen];
	pageSite[siteLen]=0;
	// site MUST NOT start with "http://"
	if ( strncmp ( pageSite , "http://", 7)==0){char*xx=NULL;*xx=0;}
	// this must match what we hash in XmlDoc::hashNoSplit()
	sprintf ( buf , "gbsitetemplate:%lu%s", (unsigned long)*tph,pageSite );
	pageSite[siteLen]=c;
	// TODO: make sure this is a no-split termid storage thingy
	// in Msg14.cpp
	Query q;
	if ( ! q.set2 ( buf , langUnknown , false ) )
		// return true with g_errno set on error
		return true;
	// store the termid
	long long termId = q.getTermId(0);

	key144_t startKey ;
	key144_t endKey   ;
	g_posdb.makeStartKey(&startKey,termId);
	g_posdb.makeEndKey  (&endKey  ,termId);

	// get shard of that (this termlist is sharded by termid -
	// see XmlDoc.cpp::hashNoSplit() where it hashes gbsitetemplate: term)
	long shardNum = g_hostdb.getShardNumByTermId ( &startKey );

	// if ( ! m_msg36.getTermFreq ( m_collnum               ,
	// 			     0                  , // maxAge
	// 			     termId             ,
	// 			     this               ,
	// 			     gotTermFreqWrapper ,
	// 			     MAX_NICENESS       ,
	// 			     true               ,  // exact count?
	// 			     false              ,  // inc count?
	// 			     false              ,  // dec count?
	// 			     false              )) // is split?
	// 	return false;


	// just use msg0 and limit to like 1k or something
	if ( ! m_msg0.getList ( -1    , // hostid
				-1    , // ip
				-1    , // port
				0     , // maxAge
				false , // addToCache?
				RDB_POSDB ,
				m_collnum      ,
				&m_list     , // RdbList ptr
				(char *)&startKey    ,
				(char *)&endKey      ,
				1024        , // minRecSize
				this        ,
				gotTermListWrapper ,
				MAX_NICENESS       ,
				false , // err correction?
				true  , // inc tree?
				true  , // domergeobsolete
				-1    , // firstHostId
				0     , // start filenum
				-1    , // numFiles
				30    , // timeout
				-1    , // syncpoint
				-1    , // preferlocalreads
				NULL  , // msg5
				NULL  , // msg5b
				false , // isRealMerge?
				true  , // allow pg cache
				false , // focelocalindexdb
				false , // doIndexdbSplit?
				shardNum ))// force paritysplit
		return false;


	// did not block
	return gotTermFreq();
}

// void gotTermFreqWrapper ( void *state ) {
// 	Images *THIS = (Images *)state;
// 	// process/store the reply
// 	if ( ! THIS->gotTermFreq() ) return;
// 	// all done
// 	THIS->m_callback ( THIS->m_state );
// }

// returns false if blocked, true otherwise
bool Images::gotTermFreq ( ) {
	// error?
	if ( g_errno ) return true;
	// bail if less than 10
	//long long nt = m_msg36.getTermFreq();
	// each key but the first is 12 bytes (compressed)
	long long nt = (m_list.getListSize() - 6)/ 12;
	// . return true, without g_errno set, we are done
	// . if we do not have 10 or more webpages that share this same 
	//   template then do not do image extraction at all, it is too risky
	//   that we get a bad image
	// . MDW: for debugging, do not require 10 pages of same template
	//if ( nt < 10 ) return true;
	if ( nt < -2 ) return true;
	// now see which of the image urls are unique
	if ( ! launchRequests () ) return false;
	// i guess we did not block
	return true;
}

// . returns false if blocked, true otherwise
// . see if other pages we've indexed have this same image url
bool Images::launchRequests ( ) {
	// loop over all images
	for ( long i = m_i ; i < m_numImages ; i++ ) {
		// advance
		m_i++;
		// assume no error
		m_errors[i] = 0;
		// make the keys. each term is a gbimage:<imageUrl> term
		// so we are searching for the image url to see how often
		// it is repeated on other pages.
		key144_t startKey ; 
		key144_t endKey   ;
		g_posdb.makeStartKey(&startKey,m_termIds[i]);
		g_posdb.makeEndKey  (&endKey  ,m_termIds[i]);
		// get our residing groupid
		//unsigned long gid = g_indexdb.getNoSplitGroupId(&startKey);
		// no split is true for this one, so we do not split by docid
		//uint32_t gid = getGroupId(RDB_INDEXDB,&startKey,false);
		unsigned long shardNum;
		shardNum = getShardNum(RDB_POSDB,&startKey);
		// get the termlist
		if ( ! m_msg0.getList ( -1    , // hostid
					-1    , // ip
					-1    , // port
					0     , // maxAge
					false , // addToCache?
					RDB_POSDB,
					m_collnum      ,
					&m_list     , // RdbList ptr
					(char *)&startKey    ,
					(char *)&endKey      ,
					1024        , // minRecSize
					this        ,
					gotTermListWrapper ,
					MAX_NICENESS       ,
					false , // err correction?
					true  , // inc tree?
					true  , // domergeobsolete
					-1    , // firstHostId
					0     , // start filenum
					-1    , // numFiles
					30    , // timeout
					-1    , // syncpoint
					-1    , // preferlocalreads
					NULL  , // msg5
					NULL  , // msg5b
					false , // isRealMerge?
					true  , // allow pg cache
					false , // focelocalindexdb
					false , // doIndexdbSplit?
					shardNum ))// force paritysplit
			return false;
		// process the msg36 response
		gotTermList ();
	}
	// i guess we didn't block
	return downloadImages();
}

// we got a reply
void gotTermListWrapper ( void *state ) {
	Images *THIS = (Images *)state;
	// process/store the reply
	THIS->gotTermList();
	// try to launch more, returns false if it blocks
	if ( ! THIS->launchRequests() ) return;
	// all done
	THIS->m_callback ( THIS->m_state );
}

void Images::gotTermList ( ) {
	long i = m_i - 1;
	// i guess get errors too
	if ( g_errno ) m_errors[i] = g_errno;
	// set a globalish flag
	if ( ! m_hadError ) m_hadError = g_errno;
	// on error skip this
	if ( g_errno ) return;
	// check docids in termlist
	m_list.resetListPtr();
	// loop over it
	for ( ; ! m_list.isExhausted() ; m_list.skipCurrentRecord() ) {
		// get the first rec
		long long d = m_list.getCurrentDocId();
		// is it us? if so ignore it
		if ( d == m_docId ) continue;
		// crap, i guess our image url is not unique. mark it off.
		m_errors[i] = EDOCDUP;
		// no need to go further
		break;
	}
}

bool Images::downloadImages () {
	// all done if we got a valid thumbnail
	//if ( m_thumbnailValid ) return true;

	long  srcLen;
	char *src = NULL;
	long node;

	// downloading an image from diffbot json reply?
	if ( m_xd->m_isDiffbotJSONObject ) {
		// i guess this better not block cuz we'll core!
		char **iup = m_xd->getDiffbotPrimaryImageUrl();
		// if no image, nothing to download
		if ( ! *iup ) {
			//log("no diffbot image url for %s",
			//    m_xd->m_firstUrl.m_url);
			return true;
		}
		// force image count to one
		m_numImages = 1;
		// do not error out
		m_errors[0] = 0;
		// set it to the full url
		src = *iup;
		srcLen = gbstrlen(src);
		// need this
		m_imageUrl.set ( src , srcLen );
		// jump into the for loop below
		//if ( m_phase == 0 ) goto insertionPoint;
	}

	// . download each leftover image
	// . stop as soon as we get one with good dimensions
	// . make a thumbnail of that one
	for (  ; m_j < m_numImages ; m_j++ , m_phase = 0 ) {

		// clear error
		g_errno = 0;

		if ( m_phase == 0 ) {
			// advance
			m_phase++;
			// only if not diffbot, we set "src" above for it
			if ( ! m_xd->m_isDiffbotJSONObject ) {
				// get img tag node
				node = m_imageNodes[m_j];
				// get the url of the image
				src = m_xml->getString(node,"src",&srcLen);
				// use "pageUrl" as the baseUrl
				m_imageUrl.set ( m_pageUrl , src , srcLen ); 
			}
			// if we should stop, stop
			if ( m_stopDownloading ) break;
			// skip if bad or not unique
			if ( m_errors[m_j] ) continue;
			// set status msg
			sprintf ( m_statusBuf ,"downloading image %li",m_j);
			// point to it
			if ( m_xd ) m_xd->setStatus ( m_statusBuf );
		}

		// get image ip
		if ( m_phase == 1 ) {
			// advance
			m_phase++;
			// this increments phase if it should
			if ( ! getImageIp() ) return false;
			// error?
			if ( g_errno ) continue;
		}

		// download the actual image
		if ( m_phase == 2 ) {
			// advance
			m_phase++;
			// download image data
			if ( ! downloadImage() ) return false;
			// error downloading?
			if ( g_errno ) continue;
		}

		// get thumbnail using threaded call to netpbm stuff
		if ( m_phase == 3 ) {
			// advance
			m_phase++;
			// call pnmscale etc. to make thumbnail
			if ( ! makeThumb() ) return false;
			// error downloading?
			if ( g_errno ) continue;
		}

		// error making thumb or just not a good thumb size?
		if ( ! m_thumbnailValid ) {
			// free old image we downloaded, if any
			m_msg13.reset();
			// i guess do this too, it was pointing at it in msg13
			m_imgReply = NULL;
			// try the next image candidate
			continue;
		}

		// it's a keeper
		long urlSize = m_imageUrl.getUrlLen() + 1; // include \0
		// . make our ThumbnailArray out of it
		long need = 0;
		// the array itself
		need += sizeof(ThumbnailArray);
		// and each thumbnail it contains
		need += urlSize;
		need += m_thumbnailSize;
		need += sizeof(ThumbnailInfo);
		// reserve it
		m_imageBuf.reserve ( need );
		// point to array
		ThumbnailArray *ta =(ThumbnailArray *)m_imageBuf.getBufStart();
		// set that as much as possible, version...
		ta->m_version = 0;
		// and thumb count
		ta->m_numThumbnails = 1;
		// now store the thumbnail info
		ThumbnailInfo *ti = ta->getThumbnailInfo (0);
		// and set our one thumbnail
		ti->m_origDX = m_dx;
		ti->m_origDY = m_dy;
		ti->m_dx = m_tdx;
		ti->m_dy = m_tdy;
		ti->m_urlSize = urlSize;
		ti->m_dataSize = m_thumbnailSize;
		// now copy the data over sequentially
		char *p = ti->m_buf;
		// the image url
		memcpy(p,m_imageUrl.getUrl(),urlSize);
		p += urlSize;
		// the image thumbnail data
		memcpy(p,m_imgData,m_thumbnailSize);
		p += m_thumbnailSize;
		// update buf length of course
		m_imageBuf.setLength ( p - m_imageBuf.getBufStart() );

		// validate the buffer
		m_imageBufValid = true;

		// save mem. do this after because m_imgData uses m_msg13's
		// reply buf to store the thumbnail for now...
		m_msg13.reset();
		m_imgReply = NULL;

		return true;
	}

	return true;
}

static void gotImgIpWrapper ( void *state , long ip ) {
	Images *THIS = (Images *)state;
	// control loop
	if ( ! THIS->downloadImages() ) return;
	// call callback at this point, we are done with the download loop
	THIS->m_callback ( THIS->m_state );
}

bool Images::getImageIp ( ) {
	if ( ! m_msgc.getIp ( m_imageUrl.getHost   () , 
			      m_imageUrl.getHostLen() ,
			      &m_latestIp     ,
			      this            , 
			      gotImgIpWrapper    ))
		// we blocked
		return false;
	return true;
}

static void downloadImageWrapper ( void *state ) {
	Images *THIS = (Images *)state;
	// control loop
	if ( ! THIS->downloadImages() ) return;
	// all done
	THIS->m_callback ( THIS->m_state );
}

bool Images::downloadImage ( ) {
	// error?
	if ( m_latestIp == 0 || m_latestIp == -1 ) {
		log(LOG_DEBUG,"images: ip of %s is %li (%s)",
		    m_imageUrl.getUrl(),m_latestIp,mstrerror(g_errno));
		// ignore errors
		g_errno = 0;
		return true;
	}
	CollectionRec *cr = g_collectiondb.getRec(m_collnum);
	// assume success
	m_httpStatus = 200;
	// set the request
	Msg13Request *r = &m_msg13Request;
	r->reset();
	r->m_maxTextDocLen  = 200000;
	r->m_maxOtherDocLen = 500000;
	r->m_urlIp = m_latestIp;
	if ( ! strcmp(cr->m_coll,"qatest123")) {
		r->m_useTestCache   = 1;
		r->m_addToTestCache = 1;
	}
	// url is the most important
	strcpy(r->m_url,m_imageUrl.getUrl());
	// . try to download it
	// . i guess we are ignoring hammers at this point
	if ( ! m_msg13.getDoc(r,false,this,downloadImageWrapper)) 
		return false;

	return true;
}

static void makeThumbWrapper ( void *state , ThreadEntry *t ) {
	Images *THIS = (Images *)state;
	// control loop
	if ( ! THIS->downloadImages() ) return;
	// all done
	THIS->m_callback ( THIS->m_state );
}

bool Images::makeThumb ( ) {
	// did it have an error?
	if ( g_errno ) {
		// just give up on all of them if one has an error
		log ( "image: had error downloading image on page %s: %s. "
		      "Not downloading any more.",
		      m_pageUrl->getUrl(),mstrerror(g_errno));
		// stop it
		m_stopDownloading = true;
		return true;
	}
	char *buf;
	long  bufLen, bufMaxLen;
	HttpMime mime;
	m_imgData     = NULL;
	m_imgDataSize = 0;

	log( LOG_DEBUG, "image: gotImage() entered." );
	// . if there was a problem, just ignore, don't let it stop getting
	//   the real page.
	if ( g_errno ) {
		log( "ERROR? g_errno puked: %s", mstrerror(g_errno) );
		//g_errno = 0;
		return true;
	}
	//if ( ! slot ) return true;
	// extract image data from the socket
	buf       = m_msg13.m_replyBuf;
	bufLen    = m_msg13.m_replyBufSize;
	bufMaxLen = m_msg13.m_replyBufAllocSize;
	// no image?
	if ( ! buf || bufLen <= 0 ) {
		g_errno = EBADIMG;
		return true;
	}
	// we are image candidate #i
	//long i = m_j - 1;
	// get img tag node
	// get the url of the image
	long  srcLen;
	char *src = NULL;
	if ( m_xd->m_isDiffbotJSONObject ) {
		src = *m_xd->getDiffbotPrimaryImageUrl();
		srcLen = gbstrlen(src);
	}
	else {
		long node = m_imageNodes[m_j];
		src = m_xml->getString(node,"src",&srcLen);
	}
	// set it to the full url
	Url iu;
	// use "pageUrl" as the baseUrl
	iu.set ( m_pageUrl , src , srcLen ); 
	// get the mime
	if ( ! mime.set ( buf, bufLen, &iu ) ) {		
		log ( "image: MIME.set() failed in gotImage()" );
		// give up on the remaining images then
		m_stopDownloading = true;
		g_errno = EBADIMG;
		return true;
	}
	// set the status so caller can see
	long httpStatus = mime.getHttpStatus();
	// check the status
	if ( httpStatus != 200 ) {
		log( LOG_DEBUG, "image: http status of img download is %li.",
		     m_httpStatus);
		// give up on the remaining images then
		m_stopDownloading = true;
		g_errno = EBADIMG;
		return true;
	}
	// make sure this is an image
	m_imgType = mime.getContentType();
	if ( m_imgType < CT_GIF || m_imgType > CT_TIFF ) {
		log( LOG_DEBUG, "image: gotImage() states that this image is "
		     "not in a format we currently handle." );
		// try the next image if any
		g_errno = EBADIMG;
		return true;
	}
	// get the content
	m_imgData     = buf + mime.getMimeLen();
	m_imgDataSize = bufLen - mime.getMimeLen();
	// Reset socket, so socket doesn't free the data, now we own
	// We must free the buf after thumbnail is inserted in TitleRec
	m_imgReply       = buf;//slot->m_readBuf;
	m_imgReplyLen    = bufLen;//slot->m_readBufSize;
	m_imgReplyMaxLen = bufMaxLen;//slot->m_readBufMaxSize;
	// do not let UdpServer free the reply, we own it now
	//slot->m_readBuf = NULL;

	if ( ! m_imgReply || m_imgReplyLen == 0 ) {
		log( LOG_DEBUG, "image: Returned empty image reply!" );
		g_errno = EBADIMG;
		return true;
	}

	// get next if too small
	if ( m_imgDataSize < 20 ) { g_errno = EBADIMG; return true; }

	long imageType;
	getImageInfo ( m_imgData, m_imgDataSize, &m_dx, &m_dy, &imageType );

	// log the image dimensions
	log( LOG_DEBUG,"image: Image Link: %s", iu.getUrl() );
	log( LOG_DEBUG,"image: Max Buffer Size: %lu bytes.",m_imgReplyMaxLen);
	log( LOG_DEBUG,"image: Image Original Size: %lu bytes.",m_imgReplyLen);
	log( LOG_DEBUG,"image: Image Buffer @ 0x%lx - 0x%lx",(long)m_imgReply, 
	     (long)m_imgReply+m_imgReplyMaxLen );
	log( LOG_DEBUG, "image: Size: %lupx x %lupx", m_dx, m_dy );

	// skip if bad dimensions
	if( ((m_dx < 50) || (m_dy < 50)) && ((m_dx > 0) && (m_dy > 0)) ) {
		log(LOG_DEBUG,
		    "image: Image is too small to represent a news article." );
		g_errno = EBADIMG;
		return true;
	}

	// update status
	if ( m_xd ) m_xd->setStatus ( "making thumbnail" );
	// log it
	log ( LOG_DEBUG, "image: gotImage() thumbnailing image." );
	// create the thumbnail...
	// reset this... why?
	g_errno = 0;
	// reset this since filterStart_r() will set it on error
	m_errno = 0;
	// callThread returns true on success, in which case we block
	if ( g_threads.call ( FILTER_THREAD        ,
			      MAX_NICENESS         ,
			      this                 ,
			      makeThumbWrapper    ,
			      thumbStartWrapper_r ) ) return false;
	// threads might be off
	logf ( LOG_DEBUG, "image: Calling thumbnail gen without thread.");
	thumbStartWrapper_r ( this , NULL );
	return true;
}

void *thumbStartWrapper_r ( void *state , ThreadEntry *t ) {
	Images *THIS = (Images *)state;
	THIS->thumbStart_r ( true /* am thread?*/ );
	return NULL;
}

void Images::thumbStart_r ( bool amThread ) {

	long long start = gettimeofdayInMilliseconds();

	//static char  scmd[200] = "%stopnm %s | "
	//                         "pnmscale -xysize 100 100 - | "
	//                         "ppmtojpeg - > %s";

	
	log( LOG_DEBUG, "image: thumbStart_r entered." );

	//DIR  *d;
	//char  cmd[2500];
	//sprintf( cmd, "%strash", g_hostdb.m_dir );

	makeTrashDir();

	// get thread id
	long id = getpid();

	// pass the input to the program through this file
	// rather than a pipe, since popen() seems broken
	char in[364];
	snprintf ( in , 363,"%strash/in.%li", g_hostdb.m_dir, id );
	unlink ( in );

	log( LOG_DEBUG, "image: thumbStart_r create in file." );

	// collect the output from the filter from this file
	char out[364];
	snprintf ( out , 363,"%strash/out.%li", g_hostdb.m_dir, id );
        unlink ( out );

	log( LOG_DEBUG, "image: thumbStart_r create out file." );

        // ignore errno from those unlinks        
        errno = 0;

        // Open/Create temporary file to store image to
        int   fhndl;
        if( (fhndl = open( in, O_RDWR+O_CREAT, S_IWUSR+S_IRUSR )) < 0 ) {
               log( "image: Could not open file, %s, for writing: %s - %d.",
       		    in, mstrerror( m_errno ), fhndl );
	       m_imgDataSize = 0;
       	       return;
        }

        // Write image data into temporary file
        if( write( fhndl, m_imgData, m_imgDataSize ) < 0 ) {
               log( "image: Could not write to file, %s: %s.",
       		    in, mstrerror( m_errno ) );
       	       close( fhndl );
	       unlink( in );
	       m_imgDataSize = 0;
       	       return;
        }

        // Close temporary image file now that we have finished writing
        if( close( fhndl ) < 0 ) {
               log( "image: Could not close file, %s, for writing: %s.",
       	            in, mstrerror( m_errno ) );
	       unlink( in );
	       m_imgDataSize = 0;
      	       return;
        }
	fhndl = 0;

        // Grab content type from mime
	//long imgType = mime.getContentType();
        char  ext[5];
        switch( m_imgType ) {
               case CT_GIF:
		       strcpy( ext, "gif" );
	               break;
               case CT_JPG:
		       strcpy( ext, "jpeg" );
		       break;
               case CT_PNG:
		       strcpy( ext, "png" );
		       break;
               case CT_TIFF:
		       strcpy( ext, "tiff" );
		       break;
	       case CT_BMP:
		       strcpy( ext, "bmp" );
		       break;
        } 

	long xysize = 250;//100;
	// make thumbnail a little bigger for diffbot for widget
	if ( m_xd->m_isDiffbotJSONObject ) xysize = 250;

	// i hope 2500 is big enough!
	char  cmd[2501];

	//sprintf( cmd, scmd, ext, in, out);
	char *wdir = g_hostdb.m_dir;
	snprintf( cmd, 2500 ,
		 "LD_LIBRARY_PATH=%s %s/%stopnm %s | "
		 "LD_LIBRARY_PATH=%s %s/pnmscale -xysize %li %li - | "
		 "LD_LIBRARY_PATH=%s %s/ppmtojpeg - > %s"
		 , wdir , wdir , ext , in
		 , wdir , wdir , xysize , xysize
		 , wdir , wdir , out
		 );
        
        // Call clone function for the shell to execute command
        // This call WILL BLOCK	. timeout is 30 seconds.
	//int err = my_system_r( cmd, 30 ); // m_thmbconvTimeout );
	int err = system( cmd ); // m_thmbconvTimeout );

	//if( (m_dx != 0) && (m_dy != 0) )
	//	unlink( in );
	unlink ( in );

	if ( err == 127 ) {
		m_errno = EBADENGINEER;
		log("image: /bin/sh does not exist.");
		unlink ( out );
		m_stopDownloading = true;
		return;
	}
	// this will happen if you don't upgrade glibc to 2.2.4-32 or above
	if ( err != 0 ) {
		m_errno = EBADENGINEER;
		log("image: Call to system(\"%s\") had error.",cmd);
		unlink ( out );
		m_stopDownloading = true;
		return;
	}

        // Open new file with thumbnail image
        if( (fhndl = open( out, O_RDONLY )) < 0 ) {
               log( "image: Could not open file, %s, for reading: %s.",
		    out, mstrerror( m_errno ) );
	       m_stopDownloading = true;
	       return;
        }

	if( (m_thumbnailSize = lseek( fhndl, 0, SEEK_END )) < 0 ) {
		log( "image: Seek of file, %s, returned invalid size: %ld",
		     out, m_thumbnailSize );
		m_stopDownloading = true;
		return;
	}

	if( m_thumbnailSize > m_imgReplyMaxLen ) {
		log( "image: Image thumbnail larger than buffer!" );
		log(LOG_DEBUG,"\t\t\tFile Read Bytes: %ld", m_thumbnailSize);
		log(LOG_DEBUG,"\t\t\tBuf Max Bytes  : %ld", m_imgReplyMaxLen );
		log(LOG_DEBUG, "\t\t\t-----------------------" );
		log(LOG_DEBUG, "\t\t\tDiff           : %ld", 
		     m_imgReplyMaxLen-m_thumbnailSize );
		return;

	}

	if( lseek( fhndl, 0, SEEK_SET ) < 0 ) {
		log( "image: Seek couldn't rewind file, %s.", out );
		m_stopDownloading = true;
		return;
	}

        // . Read contents back into image ptr
	// . this is somewhat of a hack since it overwrites the original img
        if( (m_thumbnailSize = read( fhndl, m_imgData, m_imgDataSize )) < 0 ) {
                log( "image: Could not read from file, %s: %s.",
 		     out, mstrerror( m_errno ) );
	        close( fhndl );
		m_stopDownloading = true;
		unlink( out );
	        return;
        }

        if( close( fhndl ) < 0 ) {
                log( "image: Could not close file, %s, for reading: %s.",
 		     out, mstrerror( m_errno ) );
		unlink( out );
		m_stopDownloading = true;
 	        return;
        }
	fhndl = 0;
       	unlink( out );
	long long stop = gettimeofdayInMilliseconds();
	// tell the loop above not to download anymore, we got one
	m_thumbnailValid = true;

	// MDW: this was m_imgReply
	getImageInfo ( m_imgData , m_thumbnailSize , &m_tdx , &m_tdy , NULL );

	// now make the meta data struct
	// <imageUrl>\0<width><height><thumbnailData>
	


	log( LOG_DEBUG, "image: Thumbnail size: %li bytes.", m_imgDataSize );
	log( LOG_DEBUG, "image: Thumbnail dx=%li dy=%li.", m_tdx,m_tdy );
	log( LOG_DEBUG, "image: Thumbnail generated in %lldms.", stop-start );
}

// . *it is the image type
void getImageInfo ( char *buf , long bufSize , 
		    long *dx , long *dy , long *it ) {

	// default to zeroes
	*dx = 0;
	*dy = 0;

	char *strPtr;
	// get the dimensions of the image
	if( (strPtr = strncasestr( buf, 20, "Exif"  )) ) {
		log(LOG_DEBUG, "image: Image Link: ");
		log(LOG_DEBUG, "image: We currently do not handle EXIF image "
		     "types." );
		// try the nextone
		return;
	}
	else if( (strPtr = strncasestr( buf, 20, "GIF"  )) ) {
		if ( it ) *it = CT_GIF;
		log( LOG_DEBUG, "image: GIF INFORMATION:" );
		if( bufSize > 9 ) {
			*dx = ((unsigned long)buf[7]) << 8;
			*dx += (unsigned char)buf[6];
			*dy = ((unsigned long)buf[9]) << 8;
			*dy += (unsigned char)buf[8];
		}
	}
	else if( (strPtr = strncasestr( buf, 20, "JFIF" )) ) {
		if ( it ) *it = CT_JPG;
		log( LOG_DEBUG, "image: JPEG INFORMATION:" );
		long i;
		for( i = 0; i < bufSize; i++ ) {
			if( bufSize < i+8 )
				break;
			if( (unsigned char)buf[i] != 0xFF ) continue;
			if( (unsigned char)buf[i+1] == 0xC0 ){
				*dy = ((unsigned long)buf[i+5]) << 8;
				*dy += (unsigned char)buf[i+6];
				*dx = ((unsigned long)buf[i+7]) << 8;
				*dx += (unsigned char)buf[i+8];
				break;
			}
			else if( (unsigned char) buf[i+1] == 0xC2 ) {
				*dy = ((unsigned long)buf[i+5]) << 8;
				*dy += (unsigned char)buf[i+6];
				*dx = ((unsigned long)buf[i+7]) << 8;
				*dx += (unsigned char)buf[i+8];
				break;
			}
		}
	}
	else if( (strPtr = strncasestr( buf, 20, "PNG" )) ) {
		if ( it ) *it = CT_PNG;
		log( LOG_DEBUG, "image: PNG INFORMATION:" );
		if( bufSize > 25 ) {
			*dx=(unsigned long)(*(unsigned long *)&buf[16]);
			*dy=(unsigned long)(*(unsigned long *)&buf[20]);
		}
	}
	else if( (strPtr = strncasestr( buf, 20, "MM" )) ) {
		if ( it ) *it = CT_TIFF;
		log( LOG_DEBUG, "image: TIFF INFORMATION:" );
		long startCnt = (unsigned long)buf[7]+4;
		for( long i = startCnt; i < bufSize; i += 12 ) {
			if( bufSize < i+10 )
				break;
			if( buf[i] != 0x01 ) continue;
			if( buf[i+1] == 0x01 )
				*dy = (unsigned long)
					(*(unsigned short *)&buf[i+8]);
			else if( buf[i+1] == 0x00 )
				*dx = (unsigned long)
					(*(unsigned short *)&buf[i+8]);
		}
	}
	else if( (strPtr = strncasestr( buf, 20, "II" )) ) {
		if ( it ) *it = CT_TIFF;
		log( LOG_DEBUG, "image: TIFF INFORMATION:" );
		long startCnt = (unsigned long)buf[7]+4;
		for( long i = startCnt; i < bufSize; i += 12 ) {
			if( bufSize < i+10 )
				break;
			if( buf[i] == 0x01 && buf[i+1] == 0x01 )
				*dy = (unsigned long)
					(*(unsigned short *)&buf[i+8]);
			if( buf[i] == 0x00 && buf[i+1] == 0x01 )
				*dx = (unsigned long)
					(*(unsigned short *)&buf[i+8]);
		}
	}
	else if( (strPtr = strncasestr( buf, 20, "BM" )) ) {
		if ( it ) *it = CT_BMP;
		log( LOG_DEBUG, "image: BMP INFORMATION:" );
		if( bufSize > 27 ) {
			*dx=(unsigned long)(*(unsigned long *)&buf[18]);
			*dy=(unsigned long)(*(unsigned long *)&buf[22]);
		}
	}
	else 
		log( LOG_DEBUG, "image: Image Corrupted? No type found in "
		     "data." );
}
