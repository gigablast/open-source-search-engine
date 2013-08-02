#include "Images.h"
#include "Query.h"
#include "Xml.h"
#include "Words.h"
#include "Sections.h"
#include "XmlDoc.h"
#include "Threads.h"
//#include "Msg16.h" // my_system_r()
#include "XmlDoc.h" // my_system_r()

// TODO: image is bad if repeated on same page, check for that

static void gotTermFreqWrapper ( void *state ) ;
static void gotTermListWrapper ( void *state ) ;
static void gotImageWrapper ( void *state ) ;
static void *thumbStartWrapper_r ( void *state , ThreadEntry *te );
static void  thumbDoneWrapper    ( void *state , ThreadEntry *te );
static void getImageInfo ( char *buf, long size, long *dx, long *dy, long *it);

Images::Images ( ) {
	reset();
}

void Images::reset() {
	m_imgData        = NULL;
	m_imgDataSize    = 0;
	m_setCalled      = false;
	m_thumbnailValid = false;
	m_imgBuf         = NULL;
	m_imgBufLen      = 0;
	m_imgBufMaxLen   = 0;
	m_numImages      = 0;
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
			     Sections *sections ) {
	// not valid for now
	m_thumbnailValid = false;
	// reset our array of image node candidates
	m_numImages = 0;
	// flag it
	m_setCalled = true;
	// strange...
	if ( m_imgBuf ) { char *xx=NULL;*xx=0; }
	// save this
	m_xml       = xml;
	m_pageUrl   = pageUrl;
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
		char *src = xml->getString(nn,nn+1,"src",&srcLen);
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
			    char *coll ,
			    char **statusPtr ,
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

	// sanity check
	if ( ! m_pageUrl ) { char *xx=NULL;*xx=0; }
	// sanity check
	if ( ! pageSite ) { char *xx=NULL;*xx=0; }
	// we need to be a permalink
	//if ( ! isPermalink ) return true;

	// save these
	m_statusPtr = statusPtr;
	// save this
	m_coll  = coll;
	m_docId = docId;

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

	if ( ! m_msg36.getTermFreq ( coll               ,
				     0                  , // maxAge
				     termId             ,
				     this               ,
				     gotTermFreqWrapper ,
				     MAX_NICENESS       ,
				     true               ,  // exact count?
				     false              ,  // inc count?
				     false              ,  // dec count?
				     false              )) // is split?
		return false;

	// did not block
	return gotTermFreq();
}

void gotTermFreqWrapper ( void *state ) {
	Images *THIS = (Images *)state;
	// process/store the reply
	if ( ! THIS->gotTermFreq() ) return;
	// all done
	THIS->m_callback ( THIS->m_state );
}

bool Images::gotTermFreq ( ) {
	// error?
	if ( g_errno ) return true;
	// bail if less than 10
	long long nt = m_msg36.getTermFreq();
	// return true, without g_errno set, we are done
	if ( nt < 10 ) return true;
	// now see which of the image urls are unique
	if ( ! launchRequests () ) return false;
	// i guess we did not block
	return true;
}

bool Images::launchRequests ( ) {
	// loop over all images
	for ( long i = m_i ; i < m_numImages ; i++ ) {
		// advance
		m_i++;
		// assume no error
		m_errors[i] = 0;
		// make the keys
		key_t startKey = g_indexdb.makeStartKey(m_termIds[i]);
		key_t endKey   = g_indexdb.makeEndKey  (m_termIds[i]);
		// get our residing groupid
		//unsigned long gid = g_indexdb.getNoSplitGroupId(&startKey);
		// no split is true for this one, so we do not split by docid
		uint32_t gid = getGroupId(RDB_INDEXDB,&startKey,false);
		// get the termlist
		if ( ! m_msg0.getList ( -1    , // hostid
					-1    , // ip
					-1    , // port
					0     , // maxAge
					false , // addToCache?
					RDB_INDEXDB ,
					m_coll      ,
					&m_list     , // RdbList ptr
					startKey    ,
					endKey      ,
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
					gid   ))// force paritysplit
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
	if ( m_thumbnailValid ) return true;
	// if not valid free old image
	if ( m_imgBuf ) {
		mfree ( m_imgBuf , m_imgBufMaxLen , "Image" );
		m_imgBuf = NULL;
	}
	// . download each leftover image
	// . stop as soon as we get one with good dimensions
	// . make a thumbnail of that one
	for ( long i = m_j ; i < m_numImages ; i++ ) {
		// advance now
		m_j++;
		// if we should stop, stop
		if ( m_stopDownloading ) break;
		// skip if bad or not unique
		if ( m_errors[i] ) continue;
		// set status msg
		sprintf ( m_statusBuf ,"downloading image %li",i);
		// point to it
		*m_statusPtr = m_statusBuf;
		// get the url of the image
		long  srcLen;
		char *src = m_xml->getString(i,i+1,"src",&srcLen);
		// set it to the full url
		Url iu;
		// use "pageUrl" as the baseUrl
		iu.set ( m_pageUrl , src , srcLen ); 
		// assume success
		m_httpStatus = 200;
		// set the request
		Msg13Request *r = &m_msg13Request;
		r->reset();
		r->m_maxTextDocLen  = 200000;
		r->m_maxOtherDocLen = 500000;
		if ( ! strcmp(m_coll,"test")) {
			r->m_useTestCache   = 1;
			r->m_addToTestCache = 1;
		}
		// url is the most important
		strcpy(r->m_url,iu.getUrl());
		// . try to download it
		// . i guess we are ignoring hammers at this point
		if ( ! m_msg13.getDoc(r,false,this,gotImageWrapper)) 
			return false;
		// handle it
		gotImage ( );
	}
	// now get the thumbnail from it
	return gotImage ( );
}

void gotImageWrapper ( void *state ) {
	Images *THIS = (Images *)state;
	// process/store the reply
	if ( ! THIS->gotImage ( ) ) return;
	// download the images. will set m_stopDownloading when we get one
	if ( ! THIS->downloadImages() ) return;
	// all done
	THIS->m_callback ( THIS->m_state );
}

bool Images::gotImage ( ) {
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

	log( LOG_DEBUG, "image: Msg16::gotImage() entered." );
	// . if there was a problem, just ignore, don't let it stop getting
	//   the real page.
	if ( g_errno ) {
		log( "ERROR? g_errno puked: %s", mstrerror(g_errno) );
		g_errno = 0;
		return true;
	}
	//if ( ! slot ) return true;
	// extract image data from the socket
	buf       = m_msg13.m_replyBuf;
	bufLen    = m_msg13.m_replyBufSize;
	bufMaxLen = m_msg13.m_replyBufAllocSize;
	// no image?
	if ( ! buf || bufLen <= 0 ) return true;
	// we are image candidate #i
	long i = m_j - 1;
	// get the url of the image
	long  srcLen;
	char *src = m_xml->getString(i,i+1,"src",&srcLen);
	// set it to the full url
	Url iu;
	// use "pageUrl" as the baseUrl
	iu.set ( m_pageUrl , src , srcLen ); 
	// get the mime
	if ( ! mime.set ( buf, bufLen, &iu ) ) {		
		log ( "image: MIME.set() failed in gotImage()" );
		// give up on the remaining images then
		m_stopDownloading = true;
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
		return true;
	}
	// make sure this is an image
	m_imgType = mime.getContentType();
	if ( m_imgType < CT_GIF || m_imgType > CT_TIFF ) {
		log( LOG_DEBUG, "image: gotImage() states that this image is "
		     "not in a format we currently handle." );
		// try the next image if any
		return true;
	}
	// get the content
	m_imgData     = buf + mime.getMimeLen();
	m_imgDataSize = bufLen - mime.getMimeLen();
	// Reset socket, so socket doesn't free the data, now we own
	// We must free the buf after thumbnail is inserted in TitleRec
	m_imgBuf       = buf;//slot->m_readBuf;
	m_imgBufLen    = bufLen;//slot->m_readBufSize;
	m_imgBufMaxLen = bufMaxLen;//slot->m_readBufMaxSize;
	// do not let UdpServer free the reply, we own it now
	//slot->m_readBuf = NULL;

	if ( ! m_imgBuf || m_imgBufLen == 0 ) {
		log( LOG_DEBUG, "image: Returned empty image data!" );
		return true;
	}

	// get next if too small
	if ( m_imgDataSize < 20 ) return true;

	long imageType;
	getImageInfo ( m_imgData, m_imgDataSize, &m_dx, &m_dy, &imageType );

	// log the image dimensions
	log( LOG_DEBUG, "image: Image Link: %s", iu.getUrl() );
	log( LOG_DEBUG, "image: Max Buffer Size: %lu bytes.",m_imgBufMaxLen );
	log( LOG_DEBUG, "image: Image Original Size: %lu bytes.",m_imgBufLen);
	log( LOG_DEBUG, "image: Image Buffer @ 0x%lx - 0x%lx.",(long)m_imgBuf, 
	     (long)m_imgBuf+m_imgBufMaxLen );
	log( LOG_DEBUG, "image: Size: %lupx x %lupx", m_dx, m_dy );

	// skip if bad dimensions
	if( ((m_dx < 50) || (m_dy < 50)) && ((m_dx > 0) && (m_dy > 0)) ) {
	    log( "image: Image is too small to represent a news article." );
	    return true;
	}

	// update status
	*m_statusPtr = "making thumbnail";
	// log it
	log ( LOG_DEBUG, "image: Msg16::gotImage() thumbnailing image." );
	// create the thumbnail...
	// reset this... why?
	g_errno = 0;
	// reset this since filterStart_r() will set it on error
	m_errno = 0;
	// callThread returns true on success, in which case we block
	if ( g_threads.call ( FILTER_THREAD        ,
			      MAX_NICENESS         ,
			      this                 ,
			      thumbDoneWrapper    ,
			      thumbStartWrapper_r ) ) return false;
	// threads might be off
	logf ( LOG_DEBUG, "image: Calling thumbnail gen without thread.");
	thumbStartWrapper_r ( NULL , NULL );
	return true;
}

void thumbDoneWrapper ( void *state , ThreadEntry *t ) {
	Images *THIS = (Images *)state;
	// . download another image if we ! m_thumbnailValid
	// . should also free m_imgBuf if ! m_thumbnailValid
	if ( ! THIS->downloadImages() ) return;
	// all done
	THIS->m_callback ( THIS->m_state );
}

void *thumbStartWrapper_r ( void *state , ThreadEntry *t ) {
	Images *THIS = (Images *)state;
	THIS->thumbStart_r ( true /* am thread?*/ );
	return NULL;
}

void Images::thumbStart_r ( bool amThread ) {

	long long start = gettimeofdayInMilliseconds();

        static char  scmd[200] = "%stopnm %s | "
	                         "pnmscale -xysize 100 100 - | "
	                         "ppmtojpeg - > %s";
	
	log( LOG_DEBUG, "image: thumbStart_r entered." );

	//DIR  *d;
        char  cmd[250];
	sprintf( cmd, "%strash", g_hostdb.m_dir );

	// get thread id
	long id = getpid();

	// pass the input to the program through this file
	// rather than a pipe, since popen() seems broken
	char in[64];
	sprintf ( in , "%strash/in.%li", g_hostdb.m_dir, id );
	unlink ( in );

	log( LOG_DEBUG, "image: thumbStart_r create in file." );

	// collect the output from the filter from this file
	char out[64];
	sprintf ( out , "%strash/out.%li", g_hostdb.m_dir, id );
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

        sprintf( cmd, scmd, ext, in, out);
        
        // Call clone function for the shell to execute command
        // This call WILL BLOCK	. timeout is 30 seconds.
        int err = my_system_r( cmd, 30 ); // m_thmbconvTimeout );

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

	if( m_thumbnailSize > m_imgBufMaxLen ) {
		log( "image: Image thumbnail larger than buffer!" );
		log( LOG_DEBUG, "\t\t\tFile Read Bytes: %ld", m_thumbnailSize);
		log( LOG_DEBUG, "\t\t\tBuf Max Bytes  : %ld", m_imgBufMaxLen );
		log( LOG_DEBUG, "\t\t\t-----------------------" );
		log( LOG_DEBUG, "\t\t\tDiff           : %ld", 
		     m_imgBufMaxLen-m_thumbnailSize );
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

	getImageInfo ( m_imgBuf , m_thumbnailSize , &m_tdx , &m_tdy , NULL );

	log( LOG_DEBUG, "image: Thumbnailed size: %li bytes.", m_imgDataSize );
	log( LOG_DEBUG, "image: Thumbnaile dx=%li dy=%li.", m_tdx,m_tdy );
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
