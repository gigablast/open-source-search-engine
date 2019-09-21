#include "Users.h"

Users g_users;
RdbTree g_testResultsTree;

// intialize User members
User::User(){
	m_permissions = 0;
	m_numTagIds   = 0;
	m_numColls    = 0;
	m_allColls    = false;
	m_numIps      = 0;
	m_allIps      = false;
	m_numPages    = 0;
	m_allPages    = 0;
	m_reLogin     = false;
	m_username[0] = '\0';
	m_password[0] = '\0';
}

// verify if user has permission from this ip
bool User::verifyIp ( int32_t ip ){
	//
	if ( m_allIps ) return true;

	// check the iplist
	for (uint16_t i=0; i < m_numIps; i++ ){
		int32_t ipCheck = m_ip[i] & ( ip & m_ipMask[i] ); 
		// check if they match
		if ( ipCheck == m_ip[i] ) return true;
	}

	return false;
}

// verify user pass
bool User::verifyPassword ( char *pass ){

	// 
	if ( ! pass ) return false;

	if ( strcmp ( pass, m_password ) ==  0 ) return true;

	return false;
}

// verify is has access to this coll
bool User::verifyColl ( int32_t collNum ){
	
	if ( m_allColls ) return true;
	//
	if ( collNum < 0 ) return false;
	
	
	for ( uint16_t i=0; i < m_numColls; i++ ) 
		if ( m_collNum[i] == collNum ) return true;

	return false;
}

// verify if the user has the supplied tagId
bool User::verifyTagId ( int32_t tagId ){

	//if ( tagId == 0 || tagId >= ST_LAST_TAG ) return false;
	if ( tagId == 0 ) return false;

	for ( uint16_t i = 0; i < m_numTagIds; i++ )
		if ( m_tagId[i] == tagId ) return true;
	
	return false;
}

// check if user is allowed to access this page 
bool User::verifyPageNum ( uint16_t pageNum ){
	if ( pageNum >= PAGE_NONE ) return false;

	for ( uint16_t i = 0; i < m_numPages; i++ ){
		bool allow = ! ( m_pages[i] & 0x8000 ); 
		if ( pageNum == (m_pages[i] & 0x7fff) ) return allow;
	}
	// check if pageNum is of dummy page
	bool isDummy = true;
	//if ( pageNum >  PAGE_PUBLIC )
	isDummy = false;
	//
	if ( m_allPages && !isDummy )
		return true;
	
	return false;
}

// get first page
int32_t User::firstPage ( ){
	// return first allowed page
	for ( uint16_t i = 0; i < m_numPages; i++ )
		if ( ! (m_pages[i] & 0x8000) ) //&&
		     //           (m_pages[i]&0x7fff) > PAGE_PUBLIC ) 
			return m_pages[i];
	
	// if all pages is set then just return the root page 
	if ( m_allPages ) return PAGE_ROOT;

	return -1;
}

// intialize the members
Users::Users(){
	m_init           = false;
	m_needsSave = false;
}

Users::~Users(){

}

bool Users::save(){
	return true;
	if ( ! m_needsSave ) return true;
	if ( ! m_loginTable.save(g_hostdb.m_dir,"userlogin.dat",NULL,0) )
		return log("users: userlogin.dat save failed");
	return true;
}

// initialize cache, tree, loadUsers and also loadTestUrls
bool Users::init(){

	// 8 byte key is an ip/usernameHash and value is the timestamp
	m_loginTable.set ( 8 , 4,0,NULL,0,false,0,"logintbl" );

	// initialize the testresults rdbtree 
	//
	int32_t nodeSize = (sizeof(key_t)+12+1) + sizeof(collnum_t);
	int32_t maxNodes      = MAX_TEST_RESULTS;
	int32_t maxMem   = maxNodes * nodeSize;
	// only need to call this once
	if ( ! g_testResultsTree.set ( 0         , // fixedDataSize
			      maxNodes  ,
			      true      , // do balancing?
			      maxMem    ,
			      false     , // own data?
			       "tree-testresults",
			      false     , // dataInPtrs
			      NULL , // dbname
			      12        , // keySize
			      false     )) 
	 	return false; 

	// call this userlogin.dat, not turkLoginTable.dat!!
	//if ( ! m_loginTable.load(g_hostdb.m_dir,"userlogin.dat", NULL,NULL) )
	//	log("users: failed to load userlogin.dat");

	// try to load the turk test results
	loadTestResults();

	// load users from the file
	m_init = loadUserHashTable ();

	m_needsSave = false;

	return m_init;
}

// load turk test results from  
bool Users::loadTestResults ( ){
	//
	File file;
	char testFile[1024];
	sprintf(testFile,"%sturkTestResults.dat", g_hostdb.m_dir );
	file.set(testFile);

	// 
	if ( ! file.doesExist() ) return false; 
		
	int32_t fileSize = file.getFileSize();
	if (fileSize <= 0 ) return false;


	// open the file
	if ( !file.open(O_RDONLY) ){
		log(LOG_DEBUG,"Users error operning test result file %s",
				testFile );
		return false;
	}

	char *buf     = NULL;
	int32_t bufSize  = 4096;
	int32_t offset   = 0;
	int32_t numNodes = 0;
	// read the turk results file
	for ( int32_t i=0; i < fileSize; ){
		// bail out if no node left in tree
		if ( numNodes >= MAX_TEST_RESULTS ) break;

		buf = (char *)mmalloc(bufSize,"UserTestResults");
		if ( !buf ){
			log(LOG_DEBUG,"Users cannot allocate mem for test"
			             " file buf");
			return false;
		}
		int32_t numBytesRead = file.read(buf,bufSize,offset);
		if ( numBytesRead < 0 ){
			log(LOG_DEBUG,"Users error reading test result file %s",
					testFile );
			mfree(buf,bufSize,"UsersTestResults");
		}
		// set the offsets	
		char *bufEnd        = buf + numBytesRead; 
		char *newLineOffset = bufEnd;
		for ( char *p = buf; p < bufEnd; p++ ){
			char      temp[250];
			char      *line = temp;
			int32_t       items = 0;
			char       username[10];
			int32_t       timestamp;
			uint16_t   result;
			while ( *p != '\n' && p < bufEnd){
				*line++ = *p++;
				if ( *p != ' ' && *p != '\n' ) continue;
				*line = '\0';
				switch(items){
				case 0: strcpy(username,temp);
					break;
				case 1: timestamp = atoi(temp);
					break;
				case 2: result    = atoi(temp);
					break;
				}
				line = temp;
				items++;
			}
			if ( p < bufEnd && *p == '\n') 
				newLineOffset = p;
			
			if ( p >= bufEnd && *p != '\n') break;
			// if the fields are not 3 then the line is corrupt
			if ( items == 3){
				key_t key;
				key.n1    = hash32n(username);
				key.n0    = ((int32_t)timestamp << 8) 
				                        | (result & 0xff);
				int32_t node =
				     g_testResultsTree.addNode(0,(char*)&key);
				if ( node < 0 ){
					log(LOG_DEBUG,"Users error adding node"
					     " to testResultsTree");
					mfree(buf,bufSize,"UsersTestResults");
					return false;
				}
				numNodes++;
			}
			//else
			//log(LOG_DEBUG,"Users corrupt line turkTestResuls file"
			//			": %s", temp);
		
		}

		// adjust the offset
		offset = file.getCurrentPos() - (int32_t)(bufEnd - newLineOffset); 
		i += bufSize - (int32_t)( bufEnd - newLineOffset );
		mfree(buf,bufSize,"UsersTestResults");
	}

	file.close();
	return true;
}

int32_t Users::getAccuracy ( char *username, time_t timestamp ){
	//
	// get key between 15 
	key_t key;
	key.n1       = hash32n(username);
	key.n0       = ((int32_t)timestamp << 8);
	int32_t refNode = g_testResultsTree.getPrevNode( 0 , (char *)&key);

	if ( refNode == -1 ) 
		refNode = g_testResultsTree.getNextNode ( 0 ,(char*)&key);
	if ( refNode == -1 ) return -1;

	// initialize the voting paramaters
	int32_t totalVotes   = 0;
	int32_t totalCorrect = 0;

	// get the vote from the reference node 
	key_t *refKey = (key_t*)g_testResultsTree.getKey(refNode);
	if ( refKey->n1 == key.n1 ){
		totalVotes++;
		if ( refKey->n0 & 0x01 ) totalCorrect++;
	}

	// scan in the forward direction 
	int32_t currentNode = refNode;
	//key_t currentKey = *refKey;
	//currentKey.n0 += 2;
	for ( int32_t i=0; i < ACCURACY_FWD_RANGE; i++ ){
		int32_t nextNode = g_testResultsTree.getNextNode(currentNode);
		if ( nextNode == -1) break;
		key_t *nextKey = (key_t *)g_testResultsTree.getKey(nextNode);
		if ( refKey->n1 == nextKey->n1 ){
			totalVotes++;
			if ( nextKey->n0 & 0x01 ) totalCorrect++;
		}	
		currentNode = nextNode;
		//	currentKey = *nextKey;
		//	currentKey.n0 += 2;
	}

	// scan in the backward direction
	currentNode = refNode;
	//currentKey = *refKey;
	//currentKey.n0 -= 2;
	for ( int32_t i=0; i < ACCURACY_BWD_RANGE; i++ ){
		int32_t prevNode = g_testResultsTree.getPrevNode(currentNode);
		if ( prevNode == -1) break;
		key_t *prevKey = (key_t *)g_testResultsTree.getKey(prevNode);
		if ( refKey->n1 == prevKey->n1 ){
			totalVotes++;
			if ( prevKey->n0 & 0x01 ) totalCorrect++;
		}	
		currentNode = prevNode;
		//currentKey = *prevKey;
		//currentKey.n0 -= 2;
	}

	// don't compute accuracy for few data points
	if ( totalVotes < ACCURACY_MIN_TESTS ) return -1;
	
	// compute accuracy in percentage
	int32_t accuracy = ( totalCorrect * 100 ) / totalVotes;
	

	return accuracy;
}

// . parses individual row of g_users
// . individual field are separated by :
//   and , is used to mention many params in single field
bool Users::parseRow (char *row, int32_t rowLen, User *user ){	
	// parse individual user row
	char *current = row;
	char *end     = &row[rowLen];
	int32_t col      = 0;
	for ( ; col < 7 ;){
		char temp[1024];
		char *p      = &temp[0];
		bool hasStar = false;
		while ( current <= end ){
			if ( *current == ',' || *current == ':' || 
			             current == end ){
				*p = '\0';
				// star is present in data
				// its allowed only for column 0 & 2
				if (*current == '*' && col != 0 && col != 2 &&
					col != 4 ){
					log(LOG_DEBUG,"Users * can only"
					    "be user for collection,ip & pages: %s",
					    row );
					return false;
				}
			
				// set the user param
				setDatum ( temp, col, user, hasStar );
				p = &temp[0];
				
				// reset hasStar for all the other columns
				// other then ip column 1
				if ( col != 1 ) hasStar = false;
				
			}
			else {
				//if ( *current == '*' || isalnum(*current) ||
				//	*current == '_' || *current=='.') { 
				if ( *current == '*' )
					hasStar = true;
				*p++ = *current;
			}
			current++;
			if ( *(current-1) == ':') break;
			//else{
				// wrong format 
			//	log(LOG_DEBUG,"Users error in log line: %s",
			//			row );
			//	return false;
			//
		}
		if ( current >= end ) break;
		col++;
		QUICKPOLL(0);
	}

	if ( current < end ) return false;

	return true;
}

// set individual user field from the given column/field
void Users::setDatum ( char *data, int32_t column, User *user, bool hasStar){
	
	int32_t dataLen = gbstrlen (data);

	if ( dataLen <= 0 || user == NULL || column < 0 ) return;

	// set the user info depending on the column
	// number or field
	switch ( column ){
	
	case 0:{ 
		if ( user->m_allColls ) break;

		if ( *data == '*' ){
			user->m_allColls = true;
			break;
		}
		collnum_t collNum = g_collectiondb.getCollnum(data);
		if (collNum >= 0 ){
			user->m_collNum[user->m_numColls] = collNum;
			user->m_numColls++;
		}
	
		break;
	}
	case 2:{ 
		if ( dataLen >= MAX_USER_SIZE ) data[MAX_USER_SIZE] = '\0'; 
		strcpy ( user->m_username, data  ); 
		break;
	}
	case 1:{
		if (user->m_allIps || user->m_numIps > MAX_IPS_PER_USER) break;
	
		// scan ip 
		// if start is present find the location of *
		uint32_t starMask = 0xffffffff;
		if ( hasStar ){
			char *p = data;
			if ( *data == '*' && *(data+1) =='\0'){
				user->m_allIps = true;
	
				break;
			}	
			// get the location of *
			unsigned char starLoc = 4;
			while ( *p !='\0'){
				if ( *p == '*'){
					// ignore the whole byte for
					// that location
					// set it to 0
					*p = '0';
					if ( starMask==0xffffffff )
						starMask >>= 8*starLoc;
				}
				if ( *p == '.' ) starLoc--;
				// starLoc = ceil(starLoc/2);
				p++;
			}
		}
		// if startMask means all ips are allowed
		if ( starMask==0 ){ user->m_allIps = true; break;}

		int32_t iplen = gbstrlen ( data );
		int32_t ip    = atoip(data,iplen);
		if ( ! ip ) break;
			
		user->m_ip[user->m_numIps]     = ip; 
		user->m_ipMask[user->m_numIps] = starMask; 
		user->m_numIps++;
		break;
	}
	case 3:{
		if ( gbstrlen(data) > MAX_PASS_SIZE ) data[MAX_PASS_SIZE] = '\0';
		strcpy ( user->m_password, data);
		break;
	}
	case 5:{
		if ( user->m_numPages >= MAX_PAGES_PER_USER ) break;
		char *p = data;
		user->m_pages[user->m_numPages]=0;
		if ( hasStar ){
			user->m_allPages = true;
			break;
		}
		// if not allowed set MSB to 1
		if ( *p == '-' ){
			user->m_pages[user->m_numPages] = 0x8000;
			p++;
		}
		int32_t pageNum = g_pages.getPageNumber(p);
		if ( pageNum < 0 || pageNum >= PAGE_NONE ){
			log(LOG_DEBUG,"Users Invalid Page - %s for user %s", p,
					user->m_username );
			break;
		}
		user->m_pages[user->m_numPages] |= ((uint16_t)pageNum & 0x7fff);
		user->m_numPages++;		
		break;
	}
	case 6:{ 
		// save the user permission
		// only one user is allowed
		// user permission keyword no longer used
		/*if ( ! user->m_permissions & 0xff ){
			if (strcmp(data,"master")==0) 
				user->m_permissions = USER_MASTER;
			else if (strcmp(data,"admin")==0)
				user->m_permissions = USER_ADMIN;
			else if (strcmp(data,"client")==0)
				user->m_permissions = USER_CLIENT;
			else if (strcmp(data,"spam")==0)
				user->m_permissions = USER_SPAM;
			else if (strcmp(data,"public")==0)
				user->m_permissions = USER_PUBLIC;
		}else{ */
			// save the tags
		int32_t     tagId  = 0;
		int32_t     strLen = gbstrlen(data);
		// backup over ^M
		if ( strLen>1 && data[strLen-1]=='M' && data[strLen-2]=='^' )
			strLen-=2;
		// 
		// skip for now, it cores for "english" because we removed
		// that tag from the list of tags in Tagdb.cpp
		//
		log("users: skipping language tag");
		break;
		tagId           = getTagTypeFromStr ( data, strLen );
		if ( tagId > 0 ) { // && tagId < ST_LAST_TAG ){
			user->m_tagId[user->m_numTagIds] = tagId;
			user->m_numTagIds++;
		}
		else {
			log(LOG_DEBUG,"Users Invalid tagname - %s for user %s", data,
					user->m_username );
			//char *xx=NULL;*xx=0;
		}
		//}
		break;
	}
	case 4:{
		if ( *data == '1' && gbstrlen(data)==1 ) user->m_reLogin=true;
		break;
	}
	default:
		//
		log(LOG_DEBUG, "Users invalid column data: %s", data);
	}
}

// . load users from Conf::g_users if size of g_users
//   changes and lastreadtime is > USER_DATA_READ_FREQ
// . returns false and sets g_errno on error
bool Users::loadUserHashTable ( ) {
       
       // read user info from the file and add to cache
	char         *buf     = &g_conf.m_users[0];
	uint32_t bufSize = g_conf.m_usersLen;
	//time_t        now     = getTimeGlobal();

	// no users?
	if ( bufSize <= 0 ) {
		//log("users: no <users> tag in gb.conf?");
		return true;
	}

	// what was this for?
	//if ( bufSize <= 0 || ( bufSize == m_oldBufSize 
	//	&& (now - m_oldBufReadTime) < USER_DATA_READ_FREQ ))
	//	return false;
	

	// init it
	if ( ! m_ht.set (12,sizeof(User),0,NULL,0,false,0,"userstbl")) 
		return false;

	// read user data from the line and add it to the cache
	char          *p = buf;
	uint32_t  i = 0;
	for ( ; i < bufSize; i++){
		// read a line from buf
		char *row   = p;
		int32_t rowLen = 0;

		while ( *p != '\r' && *p != '\n'  && i < bufSize ){
			i++; p++; rowLen++;
		}
		if ( *p == '\r' && *(p+1) == '\n' ) p+=2;
		else if ( *p == '\r' || *p == '\n' ) p++;

		if ( rowLen <= 0) break;
		
		// set "user" 
		User user; if ( ! parseRow ( row, rowLen, &user) ) continue;

		// skip empty usernames
		if ( !gbstrlen(user.m_username) || !gbstrlen(user.m_password) )
			continue;
	
		// make the user key
		key_t uk = hash32n ( user.m_username );

		// grab the slot
		int32_t slot = m_ht.getSlot ( &uk );

		// get existing User record, "eu" from hash table
		User *eu = NULL;
		if ( slot >= 0 ) eu = (User *)m_ht.getValueFromSlot ( slot );

		// add the user. will overwrite him if in there
		if ( ! m_ht.addKey ( &uk , &user ) ) return false;
	}

	return true;
}

// . get User record from user cache
// . return NULL if no record found
User *Users::getUser (char *username ) { //,bool cacheLoad){
	// bail out if init has failed
	if ( ! m_init  ) {
		log("users: could not load users from cache ");
		return NULL;
	}
	if ( ! username ) return NULL;
    	// check for user in cache
	key_t uk = hash32n ( username );
	return (User *)m_ht.getValue ( &uk );
}

// . check if user is logged
// . returns NULL if session is timedout or user not logged
// . returns the User record on success
User *Users::isUserLogged ( char *username, int32_t ip  ){
	// bail out if init has failed
        if ( !m_init ) return (User *)NULL;

	// get the user to the login cache
	// get user record from cache
	// return NULL if not found
	User *user = getUser (username);

	if ( !user ) return NULL;
	//if ( user->m_reLogin ) return user;

	// make the key a combo of ip and username
	uint64_t key;
	key = ((int64_t)ip << 32 ) | (int64_t)hash32n(username);

	int32_t slotNum = m_loginTable.getSlot ( &key );
	if ( slotNum < 0 ) return NULL;

	// if this is true, user cannot time out
	if ( user->m_reLogin ) return user;
	
	// return NULL if user sesssion has timed out
	int32_t now = getTime();
	//int32_t timestamp = m_loginTable.getValueFromSlot(slotNum);

	// let's make it a permanent login now!
	//if ( (now-timestamp) > (int32_t)USER_SESSION_TIMEOUT ){
	//	m_loginTable.removeKey(key);
	//	return NULL;
	//}

	m_needsSave = true;

	// if not timed out then add the new access time to the table
	if ( ! m_loginTable.addKey(&key,&now) )
		log("users: failed to update login of user %s : %s",username,
		    mstrerror(g_errno) );

	return user;
}

// . login the user
// . adds the user to the login table
// . the valud is the last access timestamp of user
//   which is used for session timeout
bool Users::loginUser ( char *username, int32_t ip ) {
	// bail out if init has failed
	if ( ! m_init ) return false;

	// add the user to the login table
	//key_t cacheKey = makeUserKey ( username, &cacheKey );
	uint64_t key;
	key = ((int64_t)ip << 32 ) | (int64_t)hash32n(username);

	m_needsSave = true;

	// add entry to table
	int32_t now = getTime();
	if ( m_loginTable.addKey(&key,&now) ) return true;
	return log("users: failed to login user %s : %s",
		   username,mstrerror(g_errno));
}

bool Users::logoffUser( char *username, int32_t ip ){
	uint64_t key;
	key = ((int64_t)ip << 32 ) | (int64_t)hash32n(username);
	m_loginTable.removeKey(&key);
	return true;
}

char *Users::getUsername ( HttpRequest *r ){
	// get from cgi before cookie so we can override
	char *username =  r->getString("username",NULL);
	if ( !username ) username = r->getString("user",NULL);
	// cookie is last resort
	if ( !username ) username = r->getStringFromCookie("username",NULL);
	//if ( !username ) username = r->getString("code",NULL);
	// use the password as the user name if no username given
	if ( ! username ) username = r->getString("pwd",NULL);

	return username;
}

// check page permissions
bool  Users::hasPermission ( HttpRequest *r, int32_t page , TcpSocket *s ) {

	if ( r->isLocal() ) return true;

	// get username from the request
	char *username =  getUsername(r);

	// msg28 always has permission
	if ( username &&
	     s &&
	     strcmp(username,"msg28")==0 ) {
		Host *h = g_hostdb.getHostByIp(s->m_ip);
		// we often ssh tunnel in through router0 which is also
		// the proxy, but now the proxy uses msg 0xfd to forward
		// http requests, so we no longer have to worry about this
		// being a security hazard
		//Host *p = g_hostdb.getProxyByIp(s->m_ip);
		//if ( h && ! p ) return true;
		// if host has same ip as proxy, DONT TRUST IT
		//if ( h && p->m_ip == h->m_ip ) 
		//	return  log("http: proxy ip same as host ip.");
		if ( ! h )
			return  log("http: msg28 only good internally.");
		// we are good to go
		return true;
	}

	return hasPermission ( username, page );
}

// does user have permission to view and edit the parms on this page?
bool Users::hasPermission ( char *username, int32_t page ){

	//if ( !username ) return false;
	if ( ! username ) username = "public";

	// get ths user from cache
	User  *user = getUser(username);
	if ( !user ) return false;
	
	// verify if user has access to the page
	return user->verifyPageNum(page);
}

// get the highest user level for this client
//int32_t Pages::getUserType ( TcpSocket *s , HttpRequest *r ) {
bool  Users::verifyUser ( TcpSocket *s, HttpRequest *r ){
//
	//bool isIpInNetwork = true;//g_hostdb.isIpInNetwork ( s->m_ip );

	if ( r->isLocal() ) return true;

	int32_t n = g_pages.getDynamicPageNumber ( r );	

	User *user;
	char *username = getUsername( r);

	//if ( !username ) return false;
	// if no username, assume public user. technically,
	// they are verified as who they claim to be... noone.
	if ( ! username ) return true;

	// public user need not be verified
	if ( strcmp(username,"public") == 0 ) return true;

	// user "msg28" is valid as int32_t as he is on one of the machines
	// and not the proxy ip
	if ( s && strcmp(username,"msg28")==0 ) {
		Host *h = g_hostdb.getHostByIp(s->m_ip);
		if ( h && ! h->m_isProxy ) return true;
		// if he's from 127.0.0.1 then let it slide
		//if ( s->m_ip == 16777343 ) return true;
		//if ( h && h->m_isProxy ) return true;
		// otherwise someone could do a get request with msg28
		// as the username and totally control us...
		return  log("http: msg28 only good internally and not "
			    "from proxy.");
	}

	char *password = r->getString("pwd",NULL);
	// this is the same thing!
	if ( password && ! password[0] ) password = NULL;

	/*
	// the possible proxy ip
	int32_t ip = s->m_ip;
	// . if the request is from the proxy, grab the "uip",
	//   the "user ip" who originated the query
	// . now the porxy uses msg 0xfd to forward its requests so if we
	//   receive this request from ip "ip" it is probably because we are
	//   doing an ssh tunnel through router0, which is also the proxy
	if ( g_hostdb.getProxyByIp ( ip ) ) {
		// attacker could add uip=X and gain access if we are logged
		// in through X. now we have moved the proxy off of router0
		// and onto gf49...
		return log("gb: got admin request from proxy for "
			   "user=%s. ignoring.",username);
	}
	*/

	// if the page is login then 
	// get the username from the request
	// and login the user if valid
	if ( n == PAGE_LOGIN ){
		// 
		//username = r->getString("username");
		//char *password = r->getString("pwd");

		// if no username return
		//if ( ! username ) return 0;
	
		// get the user information
		user = g_users.getUser ( username );

		// if no user by that name
		// means bad username, return
		if ( ! user ) return 0;
		
		// verify pass and return if bad
		if ( ! user->verifyPassword ( password ) ) return 0;
		
	}
	else if ( password ) {
		user = g_users.getUser(username);
		if (!user) return 0;
		//password = r->getString("pwd",NULL);
		if ( !user->verifyPassword( password) ) return 0;
		// . add the user to the login cache
		// . if we don't log him in then passing the username/pwd
		//   in the hostid links is not good enough, we'd also have
		//   to add username/pwd to all the other links too!
		g_users.loginUser(username,s->m_ip);
	}
	else {
		// check the login table and users cache
		user = g_users.isUserLogged ( username,   s->m_ip );
		// . if no user prsent return 0 to indicate that
		//   user is not logged in.
		// . MDW: no, because the cookie keeps sending
		//   username=mwells even though i don't want to login...
		//   i just want to do a search and be treated like public
		if ( ! user ) return 0;
	}

	// verify ip of the user
	bool verifyIp = user->verifyIp ( s->m_ip );
	if ( ! verifyIp ) return 0;

	// verify collection
	char *coll = r->getString ("c");
	if( !coll) coll = g_conf.m_defaultColl;
	int32_t collNum    = g_collectiondb.getCollnum(coll);
	bool verifyColl = user->verifyColl (collNum);
	if ( ! verifyColl ) return 0;
	// add the user to the login cache
	if ( n == PAGE_LOGIN || n == PAGE_LOGIN2 ){
		if ( ! g_users.loginUser ( username, s->m_ip ) ) return 0;
	}

	// now if everything is valid
	// get the user permission
	// i.e USER_MASTER | USER_ADMIN etc.
	//int32_t userType = user->getPermissions ( );

	// . Commented by Gourav
	// . Users class used
	//hif ( userType == USER_MASTER || userType ==  USER_ADMIN 
	//	|| userType == USER_PUBLIC ) 
	//	userType &= isIpInNetwork;
	
	//if ( g_conf.isMasterAdmin ( s , r ) ) return USER_MASTER;
	// see if has permission for specified collection
	//CollectionRec *cr = g_collectiondb.getRec ( r );
	// if no collection specified, assume public access
	//if ( ! cr ) return USER_PUBLIC;
	//if ( cr->hasPermission ( r , s   ) ) return USER_ADMIN;
	//if ( cr->isAssassin    ( s->m_ip ) ) return USER_SPAM;
	// otherwise, just a public user
	//return USER_PUBLIC; 

	//return userType;
	return true;
}

