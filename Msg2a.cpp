#include "gb-include.h"

#include "Msg2a.h"

#define URL_BUFFER_SIZE   (10*1024*1024)
#define CATID_BUFFER_SIZE (1024*1024)

static void gotMsg9bReplyWrapper      ( void *state );
static void handleRequest2a          ( UdpSlot *slot, int32_t niceness );
//static void gotMulticastReplyWrapper ( void *state, void *state2 );
static void gotSendReplyWrapper ( void *state, UdpSlot *slot );

// properly read from file
int32_t Msg2a::fileRead ( int fileid, void *buf, size_t count ) {
	char *p = (char*)buf;
	int32_t n = 0;
	uint32_t sizeRead = 0;
	while ( sizeRead < count ) {
		n = read ( fileid, p, count - sizeRead );
		if ( n <= 0 || n > (int32_t)count )
			return n;
		sizeRead += n;
		p += n;
	}
	return sizeRead;
}

bool Msg2a::registerHandler ( ) {
	// . register with the udp server
	if ( ! g_udpServer.registerHandler ( 0x2a, handleRequest2a ) )
		return false;
	//if ( ! g_udpServer2.registerHandler ( 0x2a, handleRequest2a ) )
	//	return false;
	return true;
}

Msg2a::Msg2a() {
	m_updateIndexes = NULL;
	m_urls = NULL;
	m_catids = NULL;
	m_numCatids = NULL;
}

Msg2a::~Msg2a() {
}

bool Msg2a::makeCatdb( char  *coll,
		       int32_t   collLen,
		       bool   updateFromNew,
		       void  *state,
		       void (*callback)(void *st) ) {
	m_coll          = coll;
	m_collLen       = collLen;
	m_updateFromNew = updateFromNew;
	m_state         = state;
	m_callback      = callback;
	// load the content and url files
	char inFile[256];
	// url info (content) file
	if ( m_updateFromNew )
		sprintf(inFile, "%scatdb/gbdmoz.content.dat.new", g_hostdb.m_dir);
	else
		sprintf(inFile, "%scatdb/gbdmoz.content.dat", g_hostdb.m_dir);
	//m_inStream.open(inFile, ifstream::in);
	m_inStream = open(inFile, O_RDONLY);
	//if (!m_inStream.is_open()) {
	if ( m_inStream < 0 ) {
		log("db: Error openning content file: %s", inFile);
		return true;
	}
	// read in the number of urls
	m_numUrlsSent = 0;
	m_numUrlsDone = 0;
	//m_inStream.read((char*)&m_numUrls, sizeof(int32_t));
	if ( fileRead(m_inStream, &m_numUrls, sizeof(int32_t) ) != sizeof(int32_t)) {
		log("db: Error reading content file: %s", inFile);
		return gotAllReplies();
	}
	
	// create the buffer for the urls and catids
	m_urls = NULL;
	m_urlsBufferSize = URL_BUFFER_SIZE;
	m_urls = (char*)mmalloc(m_urlsBufferSize, "Msg2a");
	if (!m_urls) {
		log("db: Unable to allocate %"INT32" bytes for urls",
		    (int32_t)sizeof(char)*m_urlsBufferSize);
		g_errno = ENOMEM;
		return gotAllReplies();
	}
	m_catids = NULL;
	m_catidsBufferSize = CATID_BUFFER_SIZE;
	m_catids = (int32_t*)mmalloc(sizeof(int32_t)*m_catidsBufferSize, "Msg2a");
	if (!m_catids) {
		log("db: Unable to allocate %"INT32" bytes for catids",
		    (int32_t)sizeof(int32_t)*m_catidsBufferSize);
		g_errno = ENOMEM;
		return gotAllReplies();
	}
	m_numCatids = NULL;
	// for a fresh generate, read all the urls
	if ( !m_updateFromNew ) {
		m_numNumCatids = m_numUrls;
		m_numCatids = (unsigned char*)mmalloc(m_numNumCatids, "Msg2a");
		if (!m_numCatids) {
			log("db: Unable to allocate %"INT32" bytes for numCatids",
			    m_numNumCatids);
			g_errno = ENOMEM;
			return gotAllReplies();
		}
	}

	// if we're updating, load up the diff file
	int32_t urlp    = 0;
	int32_t catidp  = 0;
	int32_t currUrl = 0;
	m_numRemoveUrls = 0;
	if ( m_updateFromNew ) {
		// open the new diff file
		//ifstream diffInStream;
		int diffInStream;
		sprintf(inFile, "%scatdb/gbdmoz.content.dat.new.diff",
				g_hostdb.m_dir);
		//diffInStream.open(inFile, ifstream::in);
		diffInStream = open(inFile, O_RDONLY);
		//if (!diffInStream.is_open()) {
		if ( diffInStream < 0 ) {
			log("db: Error openning content file: %s", inFile);
			return gotAllReplies();
		}

		// read in the number of urls to update/add
		//diffInStream.read((char*)&m_numUpdateIndexes, sizeof(int32_t));
		if ( fileRead(diffInStream, &m_numUpdateIndexes,
			      sizeof(int32_t)) != sizeof(int32_t) ) {
			log("db: Error reading content file: %s", inFile);
			return gotAllReplies();
		}
		// read in the number of urls to remove
		//diffInStream.read((char*)&m_numRemoveUrls, sizeof(int32_t));
		if ( fileRead(diffInStream, &m_numRemoveUrls, sizeof(int32_t)) !=
				sizeof(int32_t) ) {
			log("db: Error reading content file: %s", inFile);
			return gotAllReplies();
		}
		// create the buffer for the update/add indexes
		m_updateIndexes = (int32_t*)mmalloc(
				sizeof(int32_t)*m_numUpdateIndexes, "Msg2a");
		if ( !m_updateIndexes ) {
			log("db: Unable to allocate %"INT32" bytes for "
			    "updateIndexes", m_numNumCatids);
			g_errno = ENOMEM;
			return gotAllReplies();
		}
		// . now create the numcatids buffer, include only update/add
		//   urls and removed urls
		m_numNumCatids = m_numUpdateIndexes + m_numRemoveUrls;
		m_numCatids = (unsigned char*)mmalloc(m_numNumCatids, "Msg2a");
		if (!m_numCatids) {
			log("db: Unable to allocate %"INT32" bytes for numCatids",
		    	    m_numNumCatids);
			g_errno = ENOMEM;
			return gotAllReplies();
		}
		// read in the update/add indexes
		//for ( int32_t i = 0; i < m_numUpdateIndexes &&
		//		  diffInStream.good(); i++ ) {
		for ( int32_t i = 0; i < m_numUpdateIndexes; i++ ) {
			//diffInStream.read((char*)&m_updateIndexes[i],
			//		  sizeof(int32_t));
			int32_t n = fileRead ( diffInStream,
					    &m_updateIndexes[i],
					    sizeof(int32_t) );
			if ( n < 0 || n > (int32_t)sizeof(int32_t) ) {
				log("db: Error reading content file: %s",
				    inFile);
				return gotAllReplies();
			}
			if ( n == 0 )
				break;
		}
		// read in the urls to remove
		//for ( int32_t i = 0; i < m_numRemoveUrls &&
		//		  diffInStream.good(); i++ ) {
		for ( int32_t i = 0; i < m_numRemoveUrls; i++ ) {
			int16_t urlLen;
			//diffInStream.read((char*)&urlLen, sizeof(int16_t));
			int32_t n = fileRead ( diffInStream,
					    &urlLen,
					    sizeof(int16_t) );
			if ( n < 0 || n > (int32_t)sizeof(int16_t) ) {
				log("db: Error reading content file: %s",
				    inFile);
				return gotAllReplies();
			}
			if ( n == 0 )
				break;
			if ( urlLen <= 0 ) {
				log(LOG_WARN, "db: FOUND %i LENGTH URL AT "
					      "%"INT32" FOR REMOVE, EXITING.",
					      urlLen, i );
				return gotAllReplies();
			}
			// make sure there's room in the buffer
			if (urlp + urlLen + 4 >= m_urlsBufferSize) {
				char *re_urls = (char*)mrealloc(m_urls,
					       	m_urlsBufferSize,
					       	(m_urlsBufferSize+
						       URL_BUFFER_SIZE),
					       "Msg2a");
				if (!re_urls) {
					log("db: Unable to allocate %"INT32""
					    " bytes for urls",
					    (int32_t)sizeof(char)*m_urlsBufferSize+
					    URL_BUFFER_SIZE);
					g_errno = ENOMEM;
					return gotAllReplies();
				}
				m_urls = re_urls;
				m_urlsBufferSize += URL_BUFFER_SIZE;
			}
			// insert a new line between urls
			m_urls[urlp] = '\n';
			urlp++;
			// read it in
			//diffInStream.read(&m_urls[urlp], urlLen);
			n = fileRead ( diffInStream, &m_urls[urlp], urlLen );
			if ( n < 0 || n > urlLen ) {
				log("db: Error reading content file: %s",
				    inFile);
				return gotAllReplies();
			}
			if ( n == 0 )
				break;
			// normalize it
			urlLen = g_categories->fixUrl(&m_urls[urlp], urlLen);
			urlp += urlLen;
			// null terminate
			m_urls[urlp] = '\0';
			// set to no catids
			m_numCatids[i] = 0;
			currUrl++;
		}

		// close up the diff file
		//diffInStream.clear();
		//diffInStream.close();
		close(diffInStream);
	}
	log(LOG_INFO, "db: Read %"INT32" urls to remove (%"INT32")\n",
		      currUrl, m_numRemoveUrls);

	// fill the buffers
	//int32_t currUrl = 0;
	currUrl = m_numRemoveUrls;
	int32_t currUpdateIndex = 0;
	int32_t currIndex = 0;
	//float nextPercent = 0.05f;
	log ( LOG_INFO, "db: Generating Catdb...");
	//while ( m_inStream.good() && currUrl < m_numUrls ) {
	for ( int32_t i = 0; i < m_numUrls; i++ ) {
		// for update, only read in the update/add urls, skip others
		if ( m_updateFromNew ) {
			if ( currUpdateIndex >= m_numUpdateIndexes ||
			     currIndex < m_updateIndexes[currUpdateIndex] ) {
				// skip the next url
				int16_t urlLen = 0;
				char  skipUrl[MAX_URL_LEN*2];
				//m_inStream.read((char*)&urlLen, sizeof(int16_t));
				if ( fileRead ( m_inStream,
					    &urlLen,
					    sizeof(int16_t) ) != sizeof(int16_t) ) {
					log("db: Error reading content file:"
					    " %s", inFile);
					return gotAllReplies();
				}
				//m_inStream.read(skipUrl, urlLen);
				if ( fileRead ( m_inStream,
					    skipUrl,
					    urlLen ) != urlLen ) {
					log("db: Error reading content file:"
					    " %s", inFile);
					return gotAllReplies();
				}
				currIndex++;
				continue;
			}
			currIndex++;
			currUpdateIndex++;
		}
		// read the next url
		int16_t urlLen = 0;
		//m_inStream.read((char*)&urlLen, sizeof(int16_t));
		if ( fileRead(m_inStream, &urlLen, sizeof(int16_t)) !=
				sizeof(int16_t) ) {
			log("db: Error reading content file: %s", inFile);
			return gotAllReplies();
		}
		// make sure there's room in the buffer
		if (urlp + urlLen + 4 >= m_urlsBufferSize) {
			char *re_urls = (char*)mrealloc(m_urls,
					       m_urlsBufferSize,
					       (m_urlsBufferSize+
						       URL_BUFFER_SIZE),
					       "Msg2a");
			if (!re_urls) {
				log("db: Unable to allocate %"INT32""
				    " bytes for urls",
				    m_urlsBufferSize+
				    URL_BUFFER_SIZE);
				g_errno = ENOMEM;
				return gotAllReplies();
			}
			m_urls = re_urls;
			m_urlsBufferSize += URL_BUFFER_SIZE;
		}
		// insert a new line between urls
		m_urls[urlp] = '\n';
		urlp++;
		//char *url = &m_urls[urlp];
		//m_inStream.read(&m_urls[urlp], urlLen);
		if ( fileRead(m_inStream, &m_urls[urlp], urlLen ) != urlLen) {
			log("db: Error reading content file: %s", inFile);
			return gotAllReplies();
		}
		// normalize it
		urlLen = g_categories->fixUrl(&m_urls[urlp], urlLen);
		urlp += urlLen;
		// null terminate
		m_urls[urlp] = '\0';
		currUrl++;
		// debug
		//SafeBuf sb;
		//sb.safeMemcpy(&m_urls[urlp-urlLen],urlLen);
		//sb.nullTerm();
		//log("gencat: url=%s",sb.getBufStart());

	}
	log(LOG_INFO, "db: Wrote %"INT32" urls to update (%"INT32")\n",
		      currUrl - m_numRemoveUrls, m_numUpdateIndexes);
	//currUrl = 0;
	currUrl = m_numRemoveUrls;
	currUpdateIndex = 0;
	currIndex = 0;
	//while ( m_inStream.good() && currUrl < m_numUrls ) {
	for ( int32_t i = 0; i < m_numUrls; i++ ) {
		// for update, only read in the update/add ids, skip others
		if ( m_updateFromNew ) {
			if ( currUpdateIndex >= m_numUpdateIndexes ||
			     currIndex < m_updateIndexes[currUpdateIndex] ) {
				// skip the catids
				unsigned char nSkipIds;
				int32_t          skipId;
				//m_inStream.read((char*)&nSkipIds, 1);
				if ( fileRead ( m_inStream,
					    &nSkipIds,
					    1 ) != 1 ) {
					log("db: Error reading content file:"
					    " %s", inFile);
					return gotAllReplies();
				}
				for ( int32_t si = 0; si < nSkipIds; si++ ) {
					//m_inStream.read((char*)&skipId,
					//		sizeof(int32_t));
					if ( fileRead ( m_inStream,
						    &skipId,
						    sizeof(int32_t) ) !=
							sizeof(int32_t) ) {
						log("db: Error reading "
						    "content file: %s", inFile);
						return gotAllReplies();
					}
				}
				currIndex++;
				continue;
			}
			currIndex++;
			currUpdateIndex++;
		}
		// get the number of catids
		//m_numCatids[currUrl] = 0;
		//m_inStream.read((char*)&m_numCatids[currUrl], 1);
		if ( fileRead ( m_inStream, &m_numCatids[currUrl], 1 ) != 1 ) {
			log("db: Error reading content file: %s", inFile);
			return gotAllReplies();
		}
		// make sure there's room
		if (catidp + m_numCatids[currUrl] + 1 >= m_catidsBufferSize) {
			int32_t *re_catids = (int32_t*)mrealloc(
					m_catids,
					sizeof(int32_t)*m_catidsBufferSize,
					sizeof(int32_t)*(m_catidsBufferSize+
						CATID_BUFFER_SIZE),
					"Msg2a");
			if (!re_catids) {
				log("db: Unable to allocate %"INT32""
				    " bytes for catids",
				    (int32_t)sizeof(int32_t)*m_catidsBufferSize+
				    	CATID_BUFFER_SIZE);
				g_errno = ENOMEM;
				return gotAllReplies();
			}
			m_catids = re_catids;
			m_catidsBufferSize += CATID_BUFFER_SIZE;
		}
		int32_t readSize = sizeof(int32_t)*m_numCatids[currUrl];
		//m_inStream.read((char*)&m_catids[catidp],
		//		sizeof(int32_t)*m_numCatids[currUrl]);
		if ( fileRead ( m_inStream, &m_catids[catidp], readSize ) !=
				readSize ) { 
			log("db: Error reading content file: %s", inFile);
			return gotAllReplies();
		}
		// next url
		catidp += m_numCatids[currUrl];
		currUrl++;
	}
	log(LOG_INFO, "db: Num Urls: %"INT32"  Num Links: %"INT32"",
			currUrl, catidp);
	// send the Msg9
	if (!m_msg9b.addCatRecs ( m_urls,
				  m_coll,
				  m_collLen,
				  16,
				  this,
				  gotMsg9bReplyWrapper,
				  m_numCatids,
				  m_catids ) )
		return false;
	return gotAllReplies();
}

void gotMsg9bReplyWrapper ( void *state ) {
	Msg2a *THIS = (Msg2a*)state;
	// close
	if (!THIS->gotAllReplies())
		return;
	// callback
	THIS->m_callback(THIS->m_state);
}

bool Msg2a::gotAllReplies() {
	// check for error
	if (g_errno) {
		log("db: Msg9b had error adding site recs: %s",
		    mstrerror(g_errno));
		g_errno = 0;
	}
	// close the file
	//m_inStream.clear();
	//m_inStream.close();
	close(m_inStream);
	// free the buffers
	if (m_updateIndexes) {
		mfree(m_updateIndexes,
		      sizeof(int32_t)*m_numUpdateIndexes, "Msg2a");
		m_updateIndexes = NULL;
	}
	if (m_urls) {
		mfree(m_urls, m_urlsBufferSize, "Msg2a");
		m_urls = NULL;
	}
	if (m_catids) {
		mfree(m_catids, sizeof(int32_t)*m_catidsBufferSize, "Msg2a");
		m_catids = NULL;
	}
	if (m_numCatids) {
		mfree(m_numCatids, m_numNumCatids, "Msg2a");
		m_numCatids = NULL;
	}

	// . load up the new g_categories and
	//   make the new files current if we're updating
	//if ( m_updateFromNew ) {
		// setup our data
		if ( m_updateFromNew ) m_msgData = 1;
		else                   m_msgData = 0;
		// . send a message to all hosts having them switch to the new
		//   catdb and categories
		if (!sendSwitchCatdbMsgs(-1))
			return false;
		// if there's messages left and we're here, there's a problem
		if ( g_errno || m_msgsReplied < m_numMsgsToSend )
			log(LOG_WARN, "db: Had Error Sending Catdb Switch "
				      "Message to Hosts: %s",
				      mstrerror(g_errno));
	//}

	// no block
	return true;
}

bool Msg2a::sendSwitchCatdbMsgs ( int32_t num ) {
	UdpServer *us = &g_udpServer;
	// initialize the sending process
	int32_t n = num;
	if ( num == -1 ) {
		//m_numMsgsToSend = g_hostdb.m_numShards;
		m_numMsgsToSend = g_hostdb.m_numHosts;
		m_msgsSent = 0;
		m_msgsReplied = 0;
		for ( int32_t i = 0; i < NUM_MINIMSG2AS; i++ ) {
			m_miniMsg2as[i].m_index = i;
			m_miniMsg2as[i].m_parent = this;
		}
		n = 0;
	}
	// send out the msgs
	while ( n < NUM_MINIMSG2AS && m_msgsSent < m_numMsgsToSend ) {
		// send a multicast to the next group, all hosts in group
		Host *sendHost = g_hostdb.getHost(m_msgsSent);
		if ( ! us->sendRequest ( &m_msgData,
					 1,
					 0x2a,
					 sendHost->m_ip, // ip
					 sendHost->m_port, // port
					 m_msgsSent, // hostid
					 NULL,
					 &m_miniMsg2as[n],
					 gotSendReplyWrapper,
					 3600*24*365 ) )
			// error
			return true;
		// message sent, mark it
		m_msgsSent++;
		// if original request, goto the next slot
		if ( num == -1 )
			n++;
		// otherwise wait for reply
		else
			return false;
	}
	// if we're still waiting on messages, block
	if ( m_msgsReplied < m_numMsgsToSend )
		return false;
	// otherwise we're done
	return true;
}

//void gotMulticastReplyWrapper ( void *state , void *state2 ) {
void gotSendReplyWrapper ( void *state, UdpSlot *slot ) {
	//Msg2a     *THIS     = (Msg2a*)state;
	//MiniMsg2a *miniTHIS = (MiniMsg2a*)state2;
	MiniMsg2a *miniTHIS = (MiniMsg2a*)state;
	Msg2a     *THIS     = miniTHIS->m_parent;
	// increment replies
	THIS->m_msgsReplied++;
	// clear the slot
	slot->m_sendBufAlloc = NULL;
	// send another msg
	if ( !THIS->sendSwitchCatdbMsgs(miniTHIS->m_index) )
		return;
	// if there's messages left and we're here, there's a problem
	if ( g_errno || THIS->m_msgsReplied < THIS->m_numMsgsToSend )
		log("db: Had Error Sending Catdb Switch Message "
		    "to Hosts: %s", mstrerror(g_errno));
	// done, callback
	THIS->m_callback ( THIS->m_state );
}

// handle the request for switching to the updated catdb
void handleRequest2a ( UdpSlot *slot, int32_t netnice ) {
	UdpServer *us = &g_udpServer;
	//if ( netnice == 0 ) us = &g_udpServer2;

	// get the mode from the data
	char *request = slot->m_readBuf;
	int32_t  requestSize = slot->m_readBufSize;
	if ( requestSize < 1 ) {
		log("db: Got Invalid Request Size");
		// send error reply
		us->sendErrorReply ( slot, g_errno );
		return;
	}
	char updateFromNew = *request;

	// . switch over to the catdb
	//   1. Load up the "other" categories with gbdmoz.structure.dat.new
	//   2. Point g_categories to the "other" categories
	//   3. Move the .new files to the current (destructive!)
	
	char buff[1024];
	// load the new categories to the unused categories
	Categories *otherCategories;
	if ( g_categories == &g_categories1 )
		otherCategories = &g_categories2;
	else
		otherCategories = &g_categories1;
	// load the new file
	if ( updateFromNew )
		sprintf(buff, "%scatdb/gbdmoz.structure.dat.new", g_hostdb.m_dir);
	else
		sprintf(buff, "%scatdb/gbdmoz.structure.dat", g_hostdb.m_dir);
	if (otherCategories->loadCategories(buff) != 0) {
		log("db: Loading Categories From %s Failed", buff);
		// send error reply
		us->sendErrorReply ( slot, g_errno );
		return;
	}

	// point g_categories to the new one
	Categories *oldCategories = g_categories;
	g_categories = otherCategories;
	// clean up the old categories
	oldCategories->reset();

	// if a fresh generation, just return now
	if ( !updateFromNew ) {
		// done, send reply
		us->sendReply_ass ( NULL, 0, NULL, 0, slot );
		return;
	}

	// move the current files to .old
	sprintf(buff, "mv %scatdb/content.rdf.u8 %scatdb/content.rdf.u8.old",
		      g_hostdb.m_dir, g_hostdb.m_dir);
	log ( LOG_INFO, "%s", buff);
	system ( buff );
	sprintf(buff, "mv %scatdb/structure.rdf.u8 %scatdb/structure.rdf.u8.old",
		      g_hostdb.m_dir, g_hostdb.m_dir);
	log ( LOG_INFO, "%s", buff);
	system ( buff );
	sprintf(buff, "mv %scatdb/gbdmoz.content.dat "
		      "%scatdb/gbdmoz.content.dat.old",
		      g_hostdb.m_dir, g_hostdb.m_dir);
	log ( LOG_INFO, "%s", buff);
	system ( buff );
	sprintf(buff, "mv %scatdb/gbdmoz.structure.dat "
		      "%scatdb/gbdmoz.structure.dat.old",
		      g_hostdb.m_dir, g_hostdb.m_dir);
	log ( LOG_INFO, "%s", buff);
	system ( buff );
	sprintf(buff, "mv %scatdb/gbdmoz.content.dat.diff "
		      "%scatdb/gbdmoz.content.dat.diff.old",
		      g_hostdb.m_dir, g_hostdb.m_dir);
	log ( LOG_INFO, "%s", buff);
	system ( buff );

	// move the .new files to current
	sprintf(buff, "mv %scatdb/content.rdf.u8.new %scatdb/content.rdf.u8",
		      g_hostdb.m_dir, g_hostdb.m_dir);
	log ( LOG_INFO, "%s", buff);
	system ( buff );
	sprintf(buff, "mv %scatdb/structure.rdf.u8.new %scatdb/structure.rdf.u8",
		      g_hostdb.m_dir, g_hostdb.m_dir);
	log ( LOG_INFO, "%s", buff);
	system ( buff );
	sprintf(buff, "mv %scatdb/gbdmoz.content.dat.new "
		      "%scatdb/gbdmoz.content.dat",
		      g_hostdb.m_dir, g_hostdb.m_dir);
	log ( LOG_INFO, "%s", buff);
	system ( buff );
	sprintf(buff, "mv %scatdb/gbdmoz.structure.dat.new "
		      "%scatdb/gbdmoz.structure.dat",
		      g_hostdb.m_dir, g_hostdb.m_dir);
	log ( LOG_INFO, "%s", buff);
	system ( buff );
	//sprintf(buff, "mv %scatdb/gbdmoz.content.dat.new.diff "
	//	      "%scatdb/gbdmoz.content.dat.diff",
	//	      g_hostdb.m_dir, g_hostdb.m_dir);
	//log ( LOG_INFO, "%s", buff);
	//system ( buff );

	// done, send reply
	us->sendReply_ass ( NULL, 0, NULL, 0, slot );
}
