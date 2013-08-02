#include "gb-include.h"

#include "CollectionRec.h"
#include "Pages.h"
#include "Categories.h"

// . returns false if blocked, true otherwise
// . sets g_errno on error
bool sendPageDirectory ( TcpSocket *s , HttpRequest *r ) {
	// get the collection
	long collLen = 0;
	char *coll   = r->getString("c", &collLen, NULL);
	if (!coll || collLen <= 0) {
		coll    = g_conf.m_dirColl;
		collLen = gbstrlen(coll);
	}
	// get the category
	char *path    = r->getPath();
	long  pathLen = r->getPathLen();
	// get the category id from the path
	char decodedPath[MAX_HTTP_FILENAME_LEN+2];
	// do not breach the buffer
	if ( pathLen > MAX_HTTP_FILENAME_LEN ) pathLen = MAX_HTTP_FILENAME_LEN;
	long decodedPathLen = urlDecode(decodedPath, path, pathLen);
	decodedPath[decodedPathLen] = '\0';
	// sanity check
	if ( decodedPathLen > MAX_HTTP_FILENAME_LEN ) { char*xx=NULL;*xx=0;}
	// remove cgi
	long cgiPos = 0;
	long cgiLen = 0;
	for (long i = 0; i < decodedPathLen; i++) {
		if (decodedPath[i] == '?') {
			cgiPos = i+1;
			cgiLen = decodedPathLen - cgiPos;
			decodedPathLen = i;
			break;
		}
	}
	// look it up
	long catId = g_categories->getIdFromPath(decodedPath, decodedPathLen);

	// . make a new request for PageResults
	//Url dirUrl;
	char requestBuf[1024+MAX_COLL_LEN+128];
	long requestBufSize = 1024+MAX_COLL_LEN+128;
	//g_categories.createDirectorySearchUrl ( &dirUrl,
	long requestBufLen = g_categories->createDirSearchRequest(
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
			log("directory: Could not reallocate %li bytes for"
			    //" m_readBuf", r->m_bufLen+1);
			    " m_readBuf", requestBufLen+1);
			return g_httpServer.sendErrorReply(s,500,
							   mstrerror(g_errno));
		}
		s->m_readBuf = reBuf;
		//s->m_readBufSize = r->m_bufLen+1;
		s->m_readBufSize = requestBufLen+1;
	}
	//memcpy(s->m_readBuf, r->m_buf, r->m_bufLen);
	//s->m_readBuf[r->m_bufLen] = '\0';
	memcpy(s->m_readBuf, requestBuf, requestBufLen);
	s->m_readBuf[requestBufLen] = '\0';
	// create the new search request
	//if (!r->set(s->m_readBuf, r->m_bufLen, s))
	if (!r->set(s->m_readBuf, requestBufLen, s))
		return g_httpServer.sendErrorReply(s,500, mstrerror(g_errno));
	
	// send the new request to PageResults
	return sendPageResults(s, r);
}
