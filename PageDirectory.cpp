#include "gb-include.h"

//#include "CollectionRec.h"
#include "Pages.h"
#include "Categories.h"
#include "PageResults.h" // printDMOZSubtopics()

// function is in PageRoot.cpp:
bool printDirHomePage ( SafeBuf &sb , HttpRequest *r ) ;

// . returns false if blocked, true otherwise
// . sets g_errno on error
bool sendPageDirectory ( TcpSocket *s , HttpRequest *r ) {
	// get the collection
	int32_t collLen = 0;
	char *coll   = r->getString("c", &collLen, NULL);
	if (!coll || collLen <= 0) {
		coll    = g_conf.m_dirColl;
		collLen = gbstrlen(coll);
	}
	// get the category
	char *path    = r->getPath();
	int32_t  pathLen = r->getPathLen();
	// get the category id from the path
	char decodedPath[MAX_HTTP_FILENAME_LEN+2];
	// do not breach the buffer
	if ( pathLen > MAX_HTTP_FILENAME_LEN ) pathLen = MAX_HTTP_FILENAME_LEN;
	int32_t decodedPathLen = urlDecode(decodedPath, path, pathLen);
	decodedPath[decodedPathLen] = '\0';
	// sanity check
	if ( decodedPathLen > MAX_HTTP_FILENAME_LEN ) { char*xx=NULL;*xx=0;}
	// remove cgi
	int32_t cgiPos = 0;
	int32_t cgiLen = 0;
	for (int32_t i = 0; i < decodedPathLen; i++) {
		if (decodedPath[i] == '?') {
			cgiPos = i+1;
			cgiLen = decodedPathLen - cgiPos;
			decodedPathLen = i;
			break;
		}
	}
	// look it up. returns catId <= 0 if dmoz not setup yet.
	int32_t catId = g_categories->getIdFromPath(decodedPath, decodedPathLen);

	SafeBuf sb;

	int32_t xml = r->getLong("xml",0);

	// if /Top print the directory homepage
	if ( catId == 1 || catId <= 0 ) {
		// this is in PageRoot.cpp
		if ( ! printDirHomePage(sb,r) )
			// this will be an error if dmoz not set up and
			// it and xml or json reply format requested
			return g_httpServer.sendErrorReply(s,500, 
							   mstrerror(g_errno));
	}
	//
	// try printing this shit out not as search results right now
	// but just verbatim from dmoz files
	//
	else {
		// search box
		printLogoAndSearchBox(&sb,r,catId,NULL);
		// radio buttons for search dmoz. no, this is printed
		// from call to printLogoAndSearchBox()
		//printDmozRadioButtons(sb,catId);
		// the dmoz breadcrumb
		printDMOZCrumb ( &sb,catId,xml);
		// print the subtopcis in this topic. show as links above
		// the search results
		printDMOZSubTopics ( &sb, catId , xml );
		// ok, for now just print the dmoz topics since our search
		// results will be empty... until populated!
		g_categories->printUrlsInTopic ( &sb , catId );
	}

	return g_httpServer.sendDynamicPage ( s,
					      (char*) sb.getBufStart(),
					      sb.length(),
					      // 120 seconds cachetime
					      // don't cache anymore 
					      // since
					      // we have the login bar
					      // @ the top of the page
					      0,//120, // cachetime
					      false,// post?
					      "text/html",
					      200,
					      NULL, // cookie
					      "UTF-8",
					      r);


	// . make a new request for PageResults
	//Url dirUrl;
	char requestBuf[1024+MAX_COLL_LEN+128];
	int32_t requestBufSize = 1024+MAX_COLL_LEN+128;
	//g_categories.createDirectorySearchUrl ( &dirUrl,
	log("dmoz: creating search request");
	int32_t requestBufLen = g_categories->createDirSearchRequest(
						 requestBuf,
						 requestBufSize,
						 catId,
						 r->getHost(),
						 r->getHostLen(),
						 coll,
						 collLen,
						&decodedPath[cgiPos],
						 cgiLen,
						 true , 
						 r );
	if ( requestBufLen > 1024+MAX_COLL_LEN+128 ) { char*xx=NULL;*xx=0; }
	if ( requestBufLen == 0 ) {
		g_errno = EBADREQUEST;
		log ( "directory: Unable to generate request for Directory: %s",
		      decodedPath );
		return g_httpServer.sendErrorReply(s,500, mstrerror(g_errno));
	}
	// set the Request
	//if (!r->set(&dirUrl))
	//	return g_httpServer.sendErrorReply(s,500, mstrerror(g_errno));
	// copy into s->m_readBuf, make sure there room
	//if (r->m_bufLen+1 > s->m_readBufSize) {
	if (requestBufLen+1 > s->m_readBufSize) {
		char *reBuf = (char*)mrealloc ( s->m_readBuf,
						s->m_readBufSize,
						//r->m_bufLen+1,
						requestBufLen+1,
						"PageDirectory" );
		if (!reBuf) {
			log("directory: Could not reallocate %"INT32" bytes for"
			    //" m_readBuf", r->m_bufLen+1);
			    " m_readBuf", requestBufLen+1);
			return g_httpServer.sendErrorReply(s,500,
							   mstrerror(g_errno));
		}
		s->m_readBuf = reBuf;
		//s->m_readBufSize = r->m_bufLen+1;
		s->m_readBufSize = requestBufLen+1;
	}
	//gbmemcpy(s->m_readBuf, r->m_buf, r->m_bufLen);
	//s->m_readBuf[r->m_bufLen] = '\0';
	gbmemcpy(s->m_readBuf, requestBuf, requestBufLen);
	s->m_readBuf[requestBufLen] = '\0';
	// create the new search request
	//if (!r->set(s->m_readBuf, r->m_bufLen, s))
	if (!r->set(s->m_readBuf, requestBufLen, s))
		return g_httpServer.sendErrorReply(s,500, mstrerror(g_errno));
	
	// send the new request to PageResults
	return sendPageResults(s, r);
}
