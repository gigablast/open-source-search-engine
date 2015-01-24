#include "gb-include.h"

#include "Categories.h"
#include "Catdb.h"
#include "Loop.h"
#include "sort.h"
#include "LanguageIdentifier.h"
using namespace std;

Categories  g_categories1;
Categories  g_categories2;
Categories *g_categories;

static int sortCatHash ( const void *h1, const void *h2 );

// properly read from file
int32_t Categories::fileRead ( int fileid, void *buf, size_t count ) {
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

Categories::Categories() {
	m_cats = NULL;
	m_numCats = 0;
	m_nameBuffer = NULL;
	m_nameBufferSize = 0;
	m_buffer = NULL;
	m_bufferSize = 0;
}

Categories::~Categories() {
	reset();
}

void Categories::reset() {
	if (m_buffer) {
		mfree ( m_buffer,
			m_bufferSize,
			"Categories" );
		m_buffer = NULL;
	}
}

// filename usually ./catdb/gbdmoz.structure.dat
int32_t Categories::loadCategories ( char *filename ) {
	//ifstream inStream;
	int inStream;

	// open the structure file
	inStream = open(filename, O_RDONLY);
	// make sure it opened okay
	if ( inStream < 0 ) {
		log("cat: Error opening structure file: %s", filename);
		return 1;
	}
	// read the size of the name buffer
	if ( fileRead ( inStream, &m_nameBufferSize, sizeof(int32_t) ) !=
			sizeof(int32_t) ) {
		log("cat: Error reading structure file: %s", filename);
		close(inStream);
		return 1;
	}
	// read in the number of cats
	// filename usually ./catdb/gbdmoz.structure.dat
	if ( fileRead ( inStream, &m_numCats, sizeof(int32_t) ) != sizeof(int32_t) ) {
		log("cat: Error reading structure file: %s", filename);
		close(inStream);
		return 1;
	}
	// create the name buffer
	m_bufferSize = m_nameBufferSize +
		       sizeof(Category)*m_numCats +
		       sizeof(CategoryHash)*m_numCats;
	m_buffer = (char*)mmalloc(m_bufferSize, "Categories");
	if (!m_buffer) {
		log("cat: Could not allocate %"INT32" bytes for Category Buffer",
		    m_bufferSize);
		close(inStream);
		g_errno = ENOMEM;
		return 1;
	}
	// assign the buffers
	m_nameBuffer = m_buffer;
	m_cats       = (Category*)(m_buffer + (sizeof(char)*m_nameBufferSize));
	m_catHash    = (CategoryHash*)(m_buffer +
				       (sizeof(char)*m_nameBufferSize) +
				       (sizeof(Category)*m_numCats));
				       //(sizeof(int32_t)*m_numSymParents));

	/*
	// read and fill the name buffer
	if ( fileRead ( inStream, m_nameBuffer, m_nameBufferSize ) !=
			m_nameBufferSize ) { 
		log("cat: Error reading structure file: %s", filename);
		close(inStream);
		return 1;
	}
	*/
	
	// temp buffer to read the whole file first
	int32_t readSize = m_nameBufferSize + (m_numCats * 30);
	char *tempBuffer = (char*)mmalloc(readSize, "Categories");
	if ( !tempBuffer ) {
		log("cat: Could not allocate %"INT32" bytes for File Temp Buffer",
		    readSize);
		close(inStream);
		g_errno = ENOMEM;
		return 1;
	}
	// . read the rest of the file into the temp buffer
	// . filename usually ./catdb/gbdmoz.structure.dat
	if ( fileRead ( inStream, tempBuffer, readSize ) != readSize ) {
		log("cat: Error reading structure file: %s", filename);
		close(inStream);
		return 1;
	}
	char *p = tempBuffer;
	gbmemcpy ( m_nameBuffer, p, m_nameBufferSize );
	p += m_nameBufferSize;
	
	// read and fill the cats
	for (int32_t i = 0; i < m_numCats; i++) {
		
		gbmemcpy(&m_cats[i].m_catid, p, sizeof(int32_t));
		p += sizeof(int32_t);
		gbmemcpy(&m_cats[i].m_parentid, p, sizeof(int32_t));
		p += sizeof(int32_t);
		gbmemcpy(&m_cats[i].m_nameOffset, p, sizeof(int32_t));
		p += sizeof(int32_t);
		gbmemcpy(&m_cats[i].m_nameLen, p, sizeof(int16_t));
		p += sizeof(int16_t);
		gbmemcpy(&m_cats[i].m_structureOffset, p, sizeof(int32_t));
		p += sizeof(int32_t);
		gbmemcpy(&m_cats[i].m_contentOffset, p, sizeof(int32_t));
		p += sizeof(int32_t);
		gbmemcpy(&m_cats[i].m_numUrls, p, sizeof(int32_t));
		p += sizeof(int32_t);
		
		/*
		if ( fileRead ( inStream, &m_cats[i].m_catid, sizeof(int32_t) ) !=
				sizeof(int32_t) ) {
			log("cat: Error reading structure file: %s", filename);
			close(inStream);
			return 1;
		}
		if ( fileRead(inStream, &m_cats[i].m_parentid, sizeof(int32_t)) !=
				sizeof(int32_t) ) {
			log("cat: Error reading structure file: %s", filename);
			close(inStream);
			return 1;
		}
		if ( fileRead ( inStream,
				&m_cats[i].m_nameOffset,
				sizeof(int32_t) ) != sizeof(int32_t) ) {
			log("cat: Error reading structure file: %s", filename);
			close(inStream);
			return 1;
		}
		if ( fileRead ( inStream,
				&m_cats[i].m_nameLen,
				sizeof(int16_t) ) != sizeof(int16_t) ) {
			log("cat: Error reading structure file: %s", filename);
			close(inStream);
			return 1;
		}
		if ( fileRead ( inStream, &m_cats[i].m_structureOffset,
				sizeof(int32_t) ) != sizeof(int32_t) ) {
			log("cat: Error reading structure file: %s", filename);
			close(inStream);
			return 1;
		}
		if ( fileRead ( inStream, &m_cats[i].m_contentOffset,
				sizeof(int32_t) ) != sizeof(int32_t) ) {
			log("cat: Error reading structure file: %s", filename);
			close(inStream);
			return 1;
		}
		if ( fileRead ( inStream, &m_cats[i].m_numUrls,
				sizeof(int32_t) ) != sizeof(int32_t) ) {
			log("cat: Error reading structure file: %s", filename);
			close(inStream);
			return 1;
		}
		*/
	}
	// read the category hash
	for (int32_t i = 0; i < m_numCats; i++) {
		// read the hash
		/*
		if ( fileRead ( inStream,
				&m_catHash[i].m_hash,
				sizeof(int32_t) ) != sizeof(int32_t) ) {
			log("cat: Error reading structure file: %s", filename);
			close(inStream);
			return 1;
		}
		*/
		
		gbmemcpy(&m_catHash[i].m_hash, p, sizeof(int32_t));
		p += sizeof(int32_t);
		
		// assign the index
		m_catHash[i].m_catIndex = i;
	}
	// is this a bottleneck? shouldn't it be stored that way on disk?
	int64_t start = gettimeofdayInMilliseconds();
	// sort the category hash by hash value
	gbsort(m_catHash, m_numCats, sizeof(CategoryHash), sortCatHash);

	// sanity check - no dups allowed
	uint32_t last = 0xffffffff;
	for ( int32_t i = 0 ; i < m_numCats ; i++ ) {
		if ( m_catHash[i].m_hash == last ) 
			log("dmoz: hash collision on %"UINT32"",last);
		last = m_catHash[i].m_hash;
	}

	// time it
	int64_t took = gettimeofdayInMilliseconds();
	if ( took - start > 100 ) log(LOG_INIT,"admin: Took %"INT64" ms to "
				      "sort cat hashes.",took-start);
	// close the file
	close(inStream);
	// free the temp buffer
	mfree(tempBuffer, readSize, "Categories");
	// now create the "bad" hash table, so we can quickly see if a url
	// url is in the adult, gambling or online pharmacies categories
	if ( ! makeBadHashTable() ) return 1;
	// success
	return 0;
}

// returns false and sets g_errno on error
bool Categories::makeBadHashTable ( ) {

	m_badTable.reset();

	// . if it is on disk, load it
	// . returns false and sets g_errno on load error
	// . returns true if file does not exist
	if ( ! m_badTable.load ( g_hostdb.m_dir , "badcattable.dat" ) )
		return false;

	// if it existed, we are done
	if ( m_badTable.getNumSlotsUsed() > 0 ) return true;

	log(LOG_INFO,"cat: Generating hash table of bad url hashes.");

	for ( int32_t i = 0 ; i < m_numCats ; i++ ) {
		// skip if not an bad catid
		if ( ! isIdBad ( m_cats[i].m_catid ) ) continue;
		// it is, add the url hash to the table
		addUrlsToBadHashTable ( m_cats[i].m_catid ) ;
		//log(LOG_INIT,"cat: Error making bad hash table: %s.",
		//	    mstrerror(g_errno));
		//	return false;
		//}
	}

	//log(LOG_INFO,"cat: Saving hash table to badtable.dat.");

	// now try to save it to make it faster next time around
	m_badTable.save ( g_hostdb.m_dir , "badcattable.dat" ) ;

	return true;
}

bool Categories::isInBadCat ( Url *u ) {
	// hash it
	uint32_t h = hash32 ( u->getUrl() , u->getUrlLen() );
	// if it is in there, it is in a bad catid
	if ( m_badTable.getSlot ( h ) >= 0 ) return true;
	// otherwise, not...
	return false;
}

bool Categories::isInBadCat ( uint32_t h ) {
	// if it is in there, it is in an bad catid
	if ( m_badTable.getSlot ( h ) >= 0 ) return true;
	// otherwise, not...
	return false;
}

int sortCatHash ( const void *h1, const void *h2 ) {
	if (((CategoryHash*)h1)->m_hash < ((CategoryHash*)h2)->m_hash)
		return -1;
	else if (((CategoryHash*)h1)->m_hash > ((CategoryHash*)h2)->m_hash)
		return 1;
	else
		return 0;
}

// do a binary search to get a cat from an id
int32_t Categories::getIndexFromId ( int32_t catid ) {
	int32_t low  = 0;
	int32_t high = m_numCats-1;
	int32_t currCat;
	// binary search
	while (low <= high) {
		// next check spot
		currCat = (low + high)/2;
		// check for hit
		if (m_cats[currCat].m_catid == catid)
			return currCat;
		// shift search range
		else if (m_cats[currCat].m_catid > catid)
			high = currCat-1;
		else
			low  = currCat+1;
	}
	// not found
	return -1;
}

// do a binary search to get a cat from a path
int32_t Categories::getIndexFromPath ( char *str, int32_t strLen ) {
	int32_t low  = 0;
	int32_t high = m_numCats-1;
	int32_t currCat;
	if (!str || strLen <= 0)
		return -1;
	// remove any leading /
	if (str[0] == '/') {
		str++;
		strLen--;
	}
	// remove any trailing /
	if (str[strLen-1] == '/')
		strLen--;
	// check for top
	if (strLen == 3 &&
	    strncasecmp(str, "Top", 3) == 0)
		// it is catid 2 right? but i guess zero is symbolic for us!
		return 0;
	// get the hash
	uint32_t hash = hash32Lower_a(str, strLen, 0);
	// debug
	//char c = str[strLen];
	//str[strLen] = '\0';
	//log("dmoz: looking up hash %"UINT32" for %s",hash,str);
	//str[strLen] = c;
	// binary search
	while (low <= high) {
		// next check spot
		currCat = (low + high)/2;
		// check for hit
		if (m_catHash[currCat].m_hash == hash)
			return m_catHash[currCat].m_catIndex;
		// shift search range
		else if (m_catHash[currCat].m_hash > hash)
			high = currCat-1;
		else
			low  = currCat+1;
	}
	// not found
	return -1;
}

// return the catid from the given path
int32_t Categories::getIdFromPath ( char *str, int32_t strLen ) {
	if ( ! m_cats ) return -1;
	int32_t index = getIndexFromPath(str, strLen);
	return m_cats[index].m_catid;
}

// check this ID for an RTL starter
bool Categories::isIdRTLStart ( int32_t catid ) {
	if ( catid == 88070   || // Top:World:Arabic
	     catid == 39341   || // Top:World:Farsi
	     catid == 118215  || // Top:World:Hebrew
	     catid == 1214070 || // Top:K&T:Inter:Arabic
	     catid == 1262316 || // Top:K&T:Inter:Farsi
	     catid == 910298 )   // Top:K&T:Inter:Hebrew
		return true;
	else
		return false;
}

// check this ID for an RTL starter
bool Categories::isIndexRTLStart ( int32_t catIndex ) {
	if ( catIndex > 0 )
		return isIdRTLStart(m_cats[catIndex].m_catid);
	return false;
}

// determine if a category is RTL from Id
bool Categories::isIdRTL ( int32_t catid ) {
	int32_t index = getIndexFromId(catid);
	if (index < 0)
		return false;
	return isIndexRTL(index);
}

// determine if a category is RTL from Index
bool Categories::isIndexRTL ( int32_t catIndex ) {
	int32_t currIndex = catIndex;
	while (currIndex > 0) {
		// check if this is one of the RTLs
		if (isIdRTLStart(m_cats[currIndex].m_catid))
			return true;
		// otherwise check the parent
		currIndex = getIndexFromId(m_cats[currIndex].m_parentid);
	}
	return false;
}

// check this ID for a top Adult category
bool Categories::isIdAdultStart ( int32_t catid ) {
	if ( catid == 17 )    // Top:Adult
		return true;
	else
		return false;
}

bool Categories::isIdBadStart ( int32_t catid ) {
	// Top:Adult
	if ( catid ==     17 ) 
		return true; 
	// Top:Games:Gambling
	if ( catid ==    144 ) 
		return true; 
	// Top:Shopping:Health:Pharmacy:Online_Pharmacies
	if ( catid == 128206 ) 
		return true; 
	return false;
}

// check this index for a top Adult category
bool Categories::isIndexAdultStart ( int32_t catIndex ) {
	if (catIndex > 0)
		return isIdAdultStart(m_cats[catIndex].m_catid);
	return false;
}

// check if a category is Adult from Id
bool Categories::isIdAdult ( int32_t catid ) {
	int32_t index = getIndexFromId(catid);
	if (index < 0)
		return false;
	return isIndexAdult(index);
}

// check if a category is "bad" from Id
bool Categories::isIdBad ( int32_t catid ) {
	int32_t index = getIndexFromId(catid);
	if (index < 0)
		return false;
	return isIndexBad(index);
}

// check if a category is Adult from Index
bool Categories::isIndexAdult ( int32_t catIndex ) {
	int32_t currIndex = catIndex;
	while (currIndex > 0) {
		// check if this is the Adult category
		if ( isIdAdultStart(m_cats[currIndex].m_catid) )
			return true;
		// otherwise check the parent
		currIndex = getIndexFromId(m_cats[currIndex].m_parentid);
	}
	return false;
}

// check if a category is Adult, gambling or online phrarmacy from Index
bool Categories::isIndexBad ( int32_t catIndex ) {
	int32_t currIndex = catIndex;
	while (currIndex > 0) {
		// check if this is a "bad" category
		if ( isIdBadStart(m_cats[currIndex].m_catid) )
			return true;
		// otherwise check the parent
		currIndex = getIndexFromId(m_cats[currIndex].m_parentid);
	}
	return false;
}

// print cat information
void Categories::printCats ( int32_t start, int32_t end ) {
	for (int32_t i = start; i < end; i++) {
		char str[512];
		char *s = str;
		s += sprintf(s, "Cat %"INT32":\n", i);
		s += sprintf(s, "  CatID: %"INT32"\n", m_cats[i].m_catid);
		s += sprintf(s, "  Name:  ");
		for (int32_t n = m_cats[i].m_nameOffset;
			  n < m_cats[i].m_nameOffset + m_cats[i].m_nameLen;
			  n++)
			s += sprintf(s, "%c", m_nameBuffer[n]);
		s += sprintf(s, "\n");
		s += sprintf(s, "  Name Offset:      %"INT32"\n",
				m_cats[i].m_nameOffset);
		s += sprintf(s, "  Structure Offset: %"INT32"\n",
				m_cats[i].m_structureOffset);
		s += sprintf(s, "  Content Offset:   %"INT32"\n",
				m_cats[i].m_contentOffset);
		s += sprintf(s, "  Parent:           %"INT32"\n",
				m_cats[i].m_parentid);
		s += sprintf(s, "\n");
		log ( LOG_INFO, "%s", str );
	}
}

void Categories::printPathFromId ( SafeBuf *sb ,
				   int32_t catid,
				   bool raw,
				   bool isRTL ) {
	int32_t catIndex;
	// get the index
	catIndex = getIndexFromId(catid);
	//if (catIndex < 1) return;
	printPathFromIndex(sb, catIndex, raw, isRTL);
}

void Categories::printPathFromIndex ( SafeBuf *sb ,
				      int32_t catIndex,
				      bool raw,
				      bool isRTL ) {
	int32_t parentId;
	if (catIndex < 1) return;
	// get the parent
	parentId = m_cats[catIndex].m_parentid;
	int32_t catid = m_cats[catIndex].m_catid;

	// include Top now. in newer dmoz it is catid2.
	//if ( catid == 2 ) {
	//	sb->safePrintf("Top");
	//	return;
	//}		

	// . print the parent(s) first
	// . the new dmoz data dumps signify a parentless topic by
	//   havings its parentid equal its catid, so avoid infinite
	//   loops by checking for that here now. mdw oct 2013.
	// . the new DMOZ has Top has catid 2 now, even though it is
	//   mistakenly labelled as Top/World, which is really catid 3.
	//   so make this parentId > 2...
	if (parentId >= 1 && parentId != catid ) {
		bool isParentRTL = isIdRTLStart(parentId);
		// print spacing here if RTL
		//if (isRTL && !raw)
		//	p += sprintf(p, " :");
		printPathFromId(sb, parentId, raw, isRTL);
		// print a spacing
		//if (!isRTL && !raw)
		//	p += sprintf(p, ": ");
		//else if (raw)
		//	p += sprintf(p, "/");
		if (!raw) sb->safePrintf(": ");
		else      sb->safePrintf("/");
		// if parent was the start of RTL, <br>
		if (isParentRTL && !raw)
			sb->safePrintf("</span><br>");
	}
	// print this category name
	int32_t nameLen = m_cats[catIndex].m_nameLen;
	int32_t nameOffset = m_cats[catIndex].m_nameOffset;
	if (raw) { 
		sb->safeMemcpy(&m_nameBuffer[nameOffset], nameLen);
	}
	else {
		// html encode the name
		char encodedName[2048];
		char *encodeEnd = htmlEncode ( encodedName,
					       encodedName + 2047,
					      &m_nameBuffer[nameOffset],
					      &m_nameBuffer[nameOffset] +
					       		nameLen );
		nameLen = encodeEnd - encodedName;
		// fill it, replace _ with space
		for (int32_t i = 0; i < nameLen; i++) {
			if (encodedName[i] == '_')
				sb->safePrintf(" ");
			else
				sb->safePrintf("%c", encodedName[i]);
		}
	}
}

void Categories::printPathCrumbFromId ( SafeBuf *sb ,
					int32_t catid,
					bool isRTL ) {
	int32_t catIndex;
	// get the index
	catIndex = getIndexFromId(catid);
	//if (catIndex < 1) return;
	printPathCrumbFromIndex(sb, catIndex, isRTL);
}

void Categories::printPathCrumbFromIndex ( SafeBuf *sb,
					   int32_t catIndex,
					   bool isRTL ) {
	int32_t parentId;
	if (catIndex < 1) return;
	// get the parent
	parentId = m_cats[catIndex].m_parentid;
	int32_t catid = m_cats[catIndex].m_catid;

	// include Top now. in newer dmoz it is catid2.
	// seems to already be included below... because you made it
	// parentId>1 not parentId>2
	//if ( catid == 2 ) {
	//	sb->safePrintf("Top");
	//	return;
	//}

	// . print the parent(s) first
	// . the new dmoz has Top has parentid 2 now, and Top/World is
	//   catid 3. so make this parentId > 2 not parentId > 1
	if (parentId > 1 && parentId != catid ) {
		bool isParentRTL = isIdRTLStart(parentId);
		printPathCrumbFromId(sb, parentId, isRTL);
		// print a spacing
		sb->safePrintf(": ");
		// if parent starts RTL, <br>
		if (isParentRTL && isRTL)
			sb->safePrintf("</span><br>");
	}
	// print this category's link
	sb->safePrintf("<a href=\"/");
	printPathFromIndex(sb, catIndex, true, isRTL);
	sb->safePrintf("/\">");
	int32_t nameLen = m_cats[catIndex].m_nameLen;
	int32_t nameOffset = m_cats[catIndex].m_nameOffset;
	// fill it, replace _ with space
	{
		// html encode the name
		char encodedName[2048];
		char *encodeEnd = htmlEncode ( encodedName,
					       encodedName + 2047,
					      &m_nameBuffer[nameOffset],
					      &m_nameBuffer[nameOffset] +
					       		nameLen );
		nameLen = encodeEnd - encodedName;
		for (int32_t i = 0; i < nameLen; i++) {
			if (encodedName[i] == '_')
				sb->safePrintf(" ");
			else
				sb->safePrintf("%c", encodedName[i]);
		}
	}
	sb->safePrintf("</a>");
}

// increment the ptr into the file, possibly reading the next chunk
char* Categories::incRdfPtr( int32_t skip ) {
	int32_t n;
	for (int32_t i = 0; i < skip; i++) {
		m_rdfPtr++;
		m_currOffset++;
		// pull the next chunk if we're at the end
		if (m_rdfPtr == m_rdfEnd) {
			// if nothing left, return NULL
			//if (!m_rdfStream.good())
			//	return NULL;
			// get the next chunk
			//m_rdfStream.read(m_rdfBuffer, m_rdfBufferSize);
			//n      = m_rdfStream.gcount();
			n = read ( m_rdfStream, m_rdfBuffer, m_rdfBufferSize );
			if ( n <= 0 || n > m_rdfBufferSize )
				return NULL;
			m_rdfPtr = m_rdfBuffer;
			m_rdfEnd = &m_rdfBuffer[n];
		}
	}
	return m_rdfPtr;
}

// parse the rdf file up past a given start tag
int32_t Categories::rdfParse ( char *tagName ) {
	bool inQuote = false;
	do {
		int32_t matchPos = 0;
		// move to the next tag
		while (*m_rdfPtr != '<' || inQuote ) {
			// check for quotes
			if (*m_rdfPtr == '"')
				inQuote = !inQuote;
			// next char
			if (!incRdfPtr())
				return -1;
		}
		// check if the tag is good
		do {
			if (!incRdfPtr())
				return -1;
			if (*m_rdfPtr != tagName[matchPos])
				break;
			matchPos++;
		} while (tagName[matchPos]);
		// matched if we're at the end of the tagName
		if (!tagName[matchPos]) {
			if (!incRdfPtr())
				return -1;
			return 0;
		}
		// otherwise it's not a match, keep going
		matchPos = 0;
	} while (true);
}

// move to the next tag in the file
int32_t Categories::rdfNextTag ( ) {
	bool inQuote = false;
	// move to the next tag
	while (*m_rdfPtr != '<' || inQuote ) {
		// check for quotes
		if (*m_rdfPtr == '"')
			inQuote = !inQuote;
		// next char
		if (!incRdfPtr())
			return -1;
	}
	// skip the <
	if (!incRdfPtr())
		return -1;
	// put the tag name in a buffer
	m_tagLen = 0;
	while ( *m_rdfPtr != ' ' &&
		*m_rdfPtr != '>' ) {
		// insert the current char
		if (m_tagLen < MAX_TAG_LEN) {
			m_tagRecfer[m_tagLen] = *m_rdfPtr;
			m_tagLen++;
		}
		// next char
		if (!incRdfPtr())
			return -1;
	}
	m_tagRecfer[m_tagLen] = '\0';
	// success
	return 0;
}

// fill the next quoted string into the buffer
int32_t Categories::fillNextString(char *str, int32_t max) {
	// get the next string, skip to the next quote
	while (*m_rdfPtr != '"') {
		if (!incRdfPtr())
			return -1;
	}
	// skip the quote
	if (!incRdfPtr())
		return -1;
	// . pointing at the string now
	//   dump it in the buffer
	int32_t strLen = 0;
	while (*m_rdfPtr != '"') {
		// fill the next character
		if (strLen < max) {
			str[strLen] = *m_rdfPtr;
			strLen++;
		}
		if (!incRdfPtr())
			return -1;
	}
	// step past the quote
	if (!incRdfPtr())
		return -1;
	// return the length
	return strLen;
}

// fill the next tag body into the buffer
int32_t Categories::fillNextTagBody(char *str, int32_t max) {
	// get the next string, skip to the next quote
	while (*m_rdfPtr != '>') {
		if (!incRdfPtr())
			return -1;
	}
	// skip the >
	if (!incRdfPtr())
		return -1;
	// . pointing at the string now
	//   dump it in the buffer
	int32_t strLen = 0;
	while (*m_rdfPtr != '<') {
		// fill the next character
		if (strLen < max) {
			str[strLen] = *m_rdfPtr;
			strLen++;
		}
		if (!incRdfPtr())
			return -1;
	}
	// return the length
	return strLen;
}

// fix root urls without a trailing /
int32_t Categories::fixUrl ( char *url, int32_t urlLen ) {
	// get past the first ://
	int32_t slashi = 0;
	int32_t newUrlLen = urlLen;
	while (url[slashi]   != ':' ||
	       url[slashi+1] != '/' ||
	       url[slashi+2] != '/') {
		slashi++;
		if (slashi >= urlLen)
			return urlLen;
	}
	slashi += 3;
	// remove a www.
	/*
	if (newUrlLen - slashi >= 4 &&
	    strncasecmp(&url[slashi], "www.", 4) == 0) {
		memmove(&url[slashi], &url[slashi+4], newUrlLen - (slashi+4));
		newUrlLen -= 4;
	}
	*/
	// look for //, cut down to single /
	for (; slashi < newUrlLen; slashi++) {
		if (url[slashi-1] == '/' && url[slashi] == '/') {
			memmove(&url[slashi-1],
				&url[slashi],
				 newUrlLen - slashi);
			newUrlLen--;
		}
		if (is_wspace_a(url[slashi])) {
			memmove(&url[slashi],
				&url[slashi+1],
				 newUrlLen - (slashi+1));
			newUrlLen--;
		}
	}
	// remove any trailing /
	if (url[newUrlLen-1] == '/')
		newUrlLen--;
	// return the new length
	return newUrlLen;
}

bool Categories::addUrlsToBadHashTable ( int32_t catid  ) {
	 return getTitleAndSummary ( NULL  , // urlorig
				     0     , // urloriglen
				     catid ,
				     NULL  , // title
				     0     , // titleLen
				     0     , // maxTitleLen
				     NULL  , // summ
				     0     , // summLen
				     0     , // maxSummLen
				     NULL  , // anchor
				     0     , // anchorLen
				     0     , // maxAnchorLen
				     0     , // niceness
				     true  );// just add to table
 }

// just show the urls in dmoz
bool Categories::printUrlsInTopic ( SafeBuf *sb, int32_t catid ) {
	int32_t catIndex;
	uint32_t fileOffset;
	uint32_t n;
	char* p;
	uint32_t readSize;
	char title[1024];
	char summ[5000];
	int32_t maxTitleLen = 1024;
	int32_t maxSummLen = 5000;
	int32_t titleLen;
	int32_t summLen;
	int32_t urlStrLen;
	char urlStr[MAX_URL_LEN];
	int32_t niceness = 0;
	bool printedStart = false;

	// lookup the index for this catid
	catIndex = getIndexFromId(catid);
	if (catIndex < 0)
		goto errEnd;
	// get the file offset
	fileOffset = m_cats[catIndex].m_contentOffset;

	QUICKPOLL( niceness );

	// . open the file
	char filename[512];
	sprintf(filename, "%scatdb/%s", g_hostdb.m_dir, RDFCONTENT_FILE);
	m_rdfStream = open(filename, O_RDONLY | O_NONBLOCK);
	if ( m_rdfStream < 0 ) {
		log("cat: Error Opening %s\n", filename);
		goto errEnd;
	}
	// . seek to the offset
	n = lseek ( m_rdfStream, fileOffset, SEEK_SET );
	if ( n != fileOffset ) {
		log("cat: Error seeking to Content Offset %"INT32"", fileOffset);
		goto errEnd;
	}
	// . read in a chunk
	m_rdfBuffer     = m_rdfSmallBuffer;
	m_rdfBufferSize = RDFSMALLBUFFER_SIZE;

	p = m_rdfBuffer;
	readSize = m_rdfBufferSize;
 readLoop:
	n = read ( m_rdfStream, p, readSize );
	if(n > 0 && n != readSize) {
		p += n;
		readSize -= n;
	}
	//log(LOG_WARN,"build: reading %"INT32" bytes out of %"INT32"",n,m_rdfBufferSize);
	QUICKPOLL(niceness);

	if(n < 0 && errno == EAGAIN) goto readLoop;
	
	if ( n <= 0 || n > (uint32_t)m_rdfBufferSize ) {
		log("cat: Error Reading Content");
		goto errEnd;
	}
	m_rdfPtr = m_rdfBuffer;
	m_rdfEnd = &m_rdfBuffer[n];
	m_currOffset = fileOffset;
	// . parse to the correct url
	// parse the first topic and catid
	if (rdfNextTag() < 0)
		goto errEnd;
	if (rdfNextTag() < 0)
		goto errEnd;
	// parse until "ExternalPage"
nextTag:
	QUICKPOLL((niceness));
	if (rdfNextTag() < 0)
		goto errEnd;
	// check for catid of next topic to stop looking
	if (m_tagLen == 5 &&
	    strncmp(m_tagRecfer, "catid", 5) == 0)
		goto errEnd;
	if (m_tagLen != 12 ) goto nextTag;
	if ( strncmp(m_tagRecfer, "ExternalPage", 12) != 0) goto nextTag;

	//
	// got one
	//

	// get the next string
	urlStrLen = fillNextString(urlStr, MAX_URL_LEN-1);
	if (urlStrLen < 0)
		goto errEnd;

	// html decode the url
	/*
	urlStrLen = htmlDecode(decodedUrl, urlStr, urlStrLen,false,
			       niceness);
	gbmemcpy(urlStr, decodedUrl, urlStrLen);

	normUrl.set(urlStr, urlStrLen, true);
	g_catdb.normalizeUrl(&normUrl, &normUrl);
	// copy it back
	urlStrLen = normUrl.getUrlLen();
	gbmemcpy(urlStr, normUrl.getUrl(), urlStrLen);
	// make sure there's a trailing / on root urls
	// and no www.
	//urlStrLen = fixUrl(urlStr, urlStrLen);
	// check for an anchor
	urlAnchor = NULL;
	urlAnchorLen = 0;
	//for (int32_t i = 0; i < urlStrLen; i++) {
	//if (urlStr[i] == '#') {
	if (normUrl.getAnchorLen() > 0) {
		//urlAnchor = &urlStr[i];
		//urlAnchorLen = urlStrLen - i;
		//urlStrLen = i;
		urlAnchor = normUrl.getAnchor();
		urlAnchorLen = normUrl.getAnchorLen();
		//break;
	}
	*/

	// . parse out the title
	if (rdfParse("d:Title") < 0)
		goto errEnd;

	titleLen = fillNextTagBody(title, maxTitleLen);

	QUICKPOLL(niceness);

	// . parse out the summary
	if (rdfParse("d:Description") < 0)
		goto errEnd;

	summLen = fillNextTagBody(summ, maxSummLen);

	if ( ! printedStart ) {
		printedStart = true;
		sb->safePrintf("<ul>");
	}

	// print it out
	sb->safePrintf("<li><a href=\"");
	sb->safeMemcpy ( urlStr , urlStrLen );
	sb->safePrintf("\">");
	sb->safeMemcpy ( title , titleLen );
	sb->safePrintf("</a><br>");
	sb->safeMemcpy( summ, summLen );
	sb->safePrintf("<br>");//<br>");


	/*
	// . fill the anchor
	if (anchor) {
		if (urlAnchor) {
			if (urlAnchorLen > maxAnchorLen)
				urlAnchorLen = maxAnchorLen;
			gbmemcpy(anchor, urlAnchor, urlAnchorLen);
			*anchorLen = urlAnchorLen;
		}
		else
			*anchorLen = 0;
	}
	*/

	// DO NEXT tag
	goto nextTag;

errEnd:

	sb->safePrintf("</ul>");

	close(m_rdfStream);
	return false;
}



// . get the title and summary for a specific url
//   and catid
bool Categories::getTitleAndSummary ( char  *urlOrig,
				      int32_t   urlOrigLen,
				      int32_t   catid,
				      char  *title,
				      int32_t  *titleLen,
				      int32_t   maxTitleLen,
				      char  *summ,
				      int32_t  *summLen,
				      int32_t   maxSummLen,
				      char  *anchor,
				      unsigned char *anchorLen,
				      int32_t   maxAnchorLen ,
				      int32_t   niceness ,
				      bool   justAddToTable ) {
	int32_t catIndex;
	uint32_t fileOffset;
	uint32_t n;
	char url[MAX_URL_LEN];
	int32_t urlLen;
	char urlStr[MAX_URL_LEN];
	int32_t urlStrLen = 0;
	char decodedUrl[MAX_URL_LEN];
	char *urlAnchor = NULL;
	int32_t urlAnchorLen = 0;
	Url  normUrl;
	char* p;
	uint32_t readSize;
	// fix the original url
	//gbmemcpy(url, urlOrig, urlOrigLen);
	//urlLen = fixUrl(url, urlOrigLen);
	normUrl.set(urlOrig, urlOrigLen, true);
	g_catdb.normalizeUrl(&normUrl, &normUrl);
	gbmemcpy(url, normUrl.getUrl(), normUrl.getUrlLen());
	urlLen = normUrl.getUrlLen();
	// lookup the index for this catid
	catIndex = getIndexFromId(catid);
	if (catIndex < 0)
		goto errEnd;
	// get the file offset
	fileOffset = m_cats[catIndex].m_contentOffset;

	QUICKPOLL( niceness );

	// . open the file
	char filename[512];
	sprintf(filename, "%scatdb/%s", g_hostdb.m_dir, RDFCONTENT_FILE);
	//m_rdfStream.clear();
	//m_rdfStream.open(filename, ifstream::in);
	m_rdfStream = open(filename, O_RDONLY | O_NONBLOCK);
	//if (!m_rdfStream.is_open()) {
	if ( m_rdfStream < 0 ) {
		log("cat: Error Opening %s\n", filename);
		goto errEnd;
	}
	// . seek to the offset
	//m_rdfStream.seekg(fileOffset, ios::beg);
	n = lseek ( m_rdfStream, fileOffset, SEEK_SET );
	//if (!m_rdfStream.good()) {
	if ( n != fileOffset ) {
		log("cat: Error seeking to Content Offset %"INT32"", fileOffset);
		goto errEnd;
	}
	// . read in a chunk
	m_rdfBuffer     = m_rdfSmallBuffer;
	m_rdfBufferSize = RDFSMALLBUFFER_SIZE;
	//m_rdfStream.read(m_rdfBuffer, m_rdfBufferSize);
	//n      = m_rdfStream.gcount();

	p = m_rdfBuffer;
	readSize = m_rdfBufferSize;
 readLoop:
	n = read ( m_rdfStream, p, readSize );
	if(n > 0 && n != readSize) {
		p += n;
		readSize -= n;
	}
	//log(LOG_WARN,"build: reading %"INT32" bytes out of %"INT32"",n,m_rdfBufferSize);
	QUICKPOLL(niceness);

	if(n < 0 && errno == EAGAIN) goto readLoop;
	
	if ( n <= 0 || n > (uint32_t)m_rdfBufferSize ) {
		log("cat: Error Reading Content");
		goto errEnd;
	}
	m_rdfPtr = m_rdfBuffer;
	m_rdfEnd = &m_rdfBuffer[n];
	m_currOffset = fileOffset;
	// . parse to the correct url
	// parse the first topic and catid
	if (rdfNextTag() < 0)
		goto errEnd;
	if (rdfNextTag() < 0)
		goto errEnd;
	// parse until "ExternalPage" and correct url or "Topic"
nextTag:
	QUICKPOLL((niceness));
	if (rdfNextTag() < 0)
		goto errEnd;
	// check for catid of next topic to stop looking
	if (m_tagLen == 5 &&
	    strncmp(m_tagRecfer, "catid", 5) == 0)
		goto errEnd;
	if (m_tagLen == 12 &&
	    strncmp(m_tagRecfer, "ExternalPage", 12) == 0) {
		// get the next string
		urlStrLen = fillNextString(urlStr, MAX_URL_LEN-1);
		if (urlStrLen < 0)
			goto errEnd;
		// html decode the url
		urlStrLen = htmlDecode(decodedUrl, urlStr, urlStrLen,false,
				       niceness);
		gbmemcpy(urlStr, decodedUrl, urlStrLen);
		// normalize with Url
		//normUrl.set(urlStr, urlStrLen, false, false, false, true);
		normUrl.set(urlStr, urlStrLen, true);
		g_catdb.normalizeUrl(&normUrl, &normUrl);
		// if we just want the hashes of all the urls, add them
		if ( justAddToTable ) {
			// but skip if not a root url... because
			// LinkText::isBadCatUrl() only checks roots...
			if ( ! normUrl.isRoot() ) goto nextTag;
			uint32_t h = hash32 ( normUrl.getUrl() ,
						   normUrl.getUrlLen() );
			m_badTable.addKey ( h , 1 );
			goto nextTag;
		}
		// copy it back
		urlStrLen = normUrl.getUrlLen();
		gbmemcpy(urlStr, normUrl.getUrl(), urlStrLen);
		// make sure there's a trailing / on root urls
		// and no www.
		//urlStrLen = fixUrl(urlStr, urlStrLen);
		// check for an anchor
		urlAnchor = NULL;
		urlAnchorLen = 0;
		//for (int32_t i = 0; i < urlStrLen; i++) {
			//if (urlStr[i] == '#') {
			if (normUrl.getAnchorLen() > 0) {
				//urlAnchor = &urlStr[i];
				//urlAnchorLen = urlStrLen - i;
				//urlStrLen = i;
				urlAnchor = normUrl.getAnchor();
				urlAnchorLen = normUrl.getAnchorLen();
				//break;
			}
		//}
		//urlStr[urlStrLen] = '\0';
		// check against the url
		if (urlStrLen == urlLen &&
		    strncasecmp(url, urlStr, urlLen) == 0)
			goto foundTag;
	}
	// miss, goto next tag
	goto nextTag;
foundTag:
	// . parse out the title
	if (rdfParse("d:Title") < 0)
		goto errEnd;
	if (title && titleLen)
		*titleLen = fillNextTagBody(title, maxTitleLen);

	QUICKPOLL(niceness);

	// . parse out the summary
	if (rdfParse("d:Description") < 0)
		goto errEnd;
	if (summ && summLen)
		*summLen = fillNextTagBody(summ, maxSummLen);
	// . fill the anchor
	if (anchor) {
		if (urlAnchor) {
			if (urlAnchorLen > maxAnchorLen)
				urlAnchorLen = maxAnchorLen;
			gbmemcpy(anchor, urlAnchor, urlAnchorLen);
			*anchorLen = urlAnchorLen;
		}
		else
			*anchorLen = 0;
	}
	// . close the file
	//m_rdfStream.clear();
	//m_rdfStream.close();
	close(m_rdfStream);
	return true;

errEnd:
	if (titleLen)
		*titleLen = 0;
	if (summLen)
		*summLen  = 0;
	if (anchor)
		*anchorLen = 0;
	//m_rdfStream.close();
	//m_rdfStream.clear();
	close(m_rdfStream);
	return false;
}

// . generate sub categories for a given catid
// . store list of SubCategories into "subCatBuf" return # stored
int32_t Categories::generateSubCats ( int32_t catid,
				   SafeBuf *subCatBuf 
				   //SubCategory *subCats,
				   //char **catBuffer,
				   //int32_t  *catBufferSize,
				   //int32_t  *catBufferLen,
				   //bool   allowRealloc 
				   ) {

	int32_t catIndex;
	uint32_t fileOffset;
	uint32_t n;
	int32_t numSubCats = 0;
	int32_t currType;
	char catStr[MAX_CATNAME_LEN];
	int32_t catStrLen;
	int32_t prefixStart;
	int32_t prefixLen;
	int32_t nameStart;
	int32_t nameLen;
	int32_t need ;
	SubCategory *cat;
	char *p ;

	//int32_t catp         = 0;
	//int32_t catBufferInc = *catBufferSize;
	// . lookup the index for this catid
	// . binary step, guessing to approximate place
	//   and then scanning from there
	catIndex = getIndexFromId(catid);
	if (catIndex < 0)
		goto errEnd;
	// get the file offset
	fileOffset = m_cats[catIndex].m_structureOffset;
	// open the structure file
	// catdb/structure.rdf.u8 in utf8
	char filename[512];
	sprintf(filename, "%scatdb/%s", g_hostdb.m_dir, RDFSTRUCTURE_FILE);
	//m_rdfStream.clear();
	//m_rdfStream.open(filename, ifstream::in);
	m_rdfStream = open(filename, O_RDONLY);
	//if (!m_rdfStream.is_open()) {
	if ( m_rdfStream < 0 ) {
		log("cat: Error Opening %s\n", filename);
		goto errEnd;
	}
	// seek to the offset
	//m_rdfStream.seekg(fileOffset, ios::beg);
	n = lseek ( m_rdfStream, fileOffset, SEEK_SET );
	//if (!m_rdfStream.good()) {
	if ( n != fileOffset ) {
		log("cat: Error seeking to Structure Offset %"INT32"", fileOffset);
		goto errEnd;
	}
	// . read in a chunk
	m_rdfBuffer     = m_rdfSmallBuffer;
	m_rdfBufferSize = RDFSMALLBUFFER_SIZE;
	//m_rdfStream.read(m_rdfBuffer, m_rdfBufferSize);
	//n      = m_rdfStream.gcount();
	n = read ( m_rdfStream, m_rdfBuffer, m_rdfBufferSize );
	if ( n <= 0 || n > (uint32_t)m_rdfBufferSize ) {
		log("cat: Error Reading Structure Offset");
		goto errEnd;
	}
	// point to the buffer we just read with m_rdfPtr
	m_rdfPtr = m_rdfBuffer;
	m_rdfEnd = &m_rdfBuffer[n];
	m_currOffset = fileOffset;
	
	// parse tags for the sub categories or until we hit /Topic
nextTag:
	// . this increments m_rdfPtr until it points to the beginning of a tag
	// . it may end up reading another chunk from disk
	// . it memcopies m_tagRecfer to be the name of the tag it points to
	if (rdfNextTag() < 0)
		goto gotSubCats;
	// check for /Topic
	if (m_tagLen == 6 &&
	    strncmp(m_tagRecfer, "/Topic", 6) == 0)
		goto gotSubCats;
	else if (m_tagLen == 7 &&
		 strncmp(m_tagRecfer, "altlang", 7) == 0)
		currType = SUBCAT_ALTLANG;
	else if (m_tagLen == 7 &&
		 strncmp(m_tagRecfer, "related", 7) == 0)
		currType = SUBCAT_RELATED;
	else if (m_tagLen == 8 &&
		 strncmp(m_tagRecfer, "symbolic", 8) == 0)
		currType = SUBCAT_SYMBOLIC;
	else if (m_tagLen == 6 &&
		 strncmp(m_tagRecfer, "narrow", 6) == 0)
		currType = SUBCAT_NARROW;
	else if (m_tagLen == 9 &&
		 strncmp(m_tagRecfer, "symbolic1", 9) == 0)
		currType = SUBCAT_SYMBOLIC1;
	else if (m_tagLen == 7 &&
		 strncmp(m_tagRecfer, "narrow1", 7) == 0)
		currType = SUBCAT_NARROW1;
	else if (m_tagLen == 9 &&
		 strncmp(m_tagRecfer, "symbolic2", 9) == 0)
		currType = SUBCAT_SYMBOLIC2;
	else if (m_tagLen == 7 &&
		 strncmp(m_tagRecfer, "narrow2", 7) == 0)
		currType = SUBCAT_NARROW2;
	else if (m_tagLen == 9 &&
		 strncmp(m_tagRecfer, "letterbar", 9) == 0)
		currType = SUBCAT_LETTERBAR;
	else
		goto nextTag;
	// read the name for this category
	catStrLen = fillNextString(catStr, MAX_CATNAME_LEN-1);
	if (catStrLen < 0)
		goto gotSubCats;
	// html decode it first
	char htmlDecoded[MAX_HTTP_FILENAME_LEN*2];
	if (catStrLen > MAX_HTTP_FILENAME_LEN*2)
		catStrLen = MAX_HTTP_FILENAME_LEN*2;
	catStrLen = htmlDecode ( htmlDecoded,
				 catStr,
				 catStrLen ,
				 false,
				 0);
	gbmemcpy(catStr, htmlDecoded, catStrLen);
	// reset this offset
	nameStart = 0;
	nameLen = catStrLen;
	// get the prefix and name position/length
	switch (currType) {
	case SUBCAT_ALTLANG:
	case SUBCAT_SYMBOLIC:
	case SUBCAT_SYMBOLIC1:
	case SUBCAT_SYMBOLIC2:
		// prefix is at the start
		prefixStart = 0;
		prefixLen   = 0;
		//nameStart   = 0;
		// go to the end of the prefix
		while (catStr[nameStart] != ':') {
			nameStart++;
			prefixLen++;
		}
		// skip the : in :Top/
		nameStart += 1;
		nameLen = catStrLen - nameStart;
		break;
	case SUBCAT_LETTERBAR:
		// prefix is the very last letter
		prefixStart = catStrLen - 1;
		prefixLen   = 1;
		// skip the Top/ for the name
		//nameStart   = 4;
		// lose the Top/, keep the end letter
		//nameLen     = catStrLen - 4;
		break;
	// . don't do this because of ltr?
	//case SUBCAT_RELATED:
	//	// prefix the entire path, minus Top
	//	prefixStart = 4;
	//	prefixLen   = catStrLen - 4;
	//	// name skips Top/
	//	nameStart = 4;
	//	nameLen   = catStrLen - 4;
	//	break;
	default:
		// prefix the last folder
		prefixStart = catStrLen;
		prefixLen = 0;
		while (catStr[prefixStart-1] != '/' &&
		       prefixStart > 0) {
			prefixStart--;
			prefixLen++;
		}
		// name skips Top/ ... no! we include Top now
		// because we need it so PageResults.cpp can call
		// currIndex=g_categories->getIndexFromPath(catName,catNameLen)
		// on this name, and it needs "Top/" because it was part
		// of the hash of the full name for the category now.
		// and we lookup the Category record by that hash
		// in getIndexFromPath().
		//nameStart = 4;
		//nameLen   = catStrLen - 4;
		break;
	}
	// . fill the next sub category
	// . fill the prefix and name in the buffer and subcat
	need = sizeof(SubCategory) + prefixLen + 1 + nameLen + 1;

	// reserve space in safebuf for it
	if ( ! subCatBuf->reserve(need) ) goto errEnd;

	// point to it in safebuf
	cat = (SubCategory *)(subCatBuf->getBuf());

	cat->m_prefixLen = prefixLen;
	cat->m_nameLen = nameLen;
	cat->m_type = currType;
	p = cat->m_buf;
	gbmemcpy ( p , catStr + prefixStart , prefixLen );
	p += prefixLen;
	*p++ = '\0';
	gbmemcpy ( p , catStr + nameStart , nameLen );
	p += nameLen;
	*p++ = '\0';

	// update safebuf length
	subCatBuf->incrementLength ( cat->getRecSize() );

	/*
	subCats[numSubCats].m_prefixOffset = catp;
	subCats[numSubCats].m_prefixLen    = prefixLen;
	if (prefixLen > 0) {
		gbmemcpy(&((*catBuffer)[catp]), &catStr[prefixStart], prefixLen);
		catp += prefixLen;
	}
	subCats[numSubCats].m_nameOffset   = catBuf->length();//catp;
	subCats[numSubCats].m_nameLen      = nameLen;
	if (nameLen > 0) {
		gbmemcpy(&((*catBuffer)[catp]), &catStr[nameStart], nameLen);
		catp += nameLen;
	}
	subCats[numSubCats].m_type         = currType;
	*/
	// next sub cat
	numSubCats++;
	if (numSubCats >= MAX_SUB_CATS) {
		log ( LOG_WARN, "categories: Attempted to load too many"
				" sub-categories, truncating." );
		goto gotSubCats;
	}
	// next tag
	goto nextTag;
gotSubCats:
	//*catBufferLen = catp;
	//m_rdfStream.close();
	//m_rdfStream.clear();
	close(m_rdfStream);
	return numSubCats;

errEnd:
	//*catBufferLen = 0;
	//m_rdfStream.close();
	//m_rdfStream.clear();
	close(m_rdfStream);
	return 0;
}

// creates a directory search request url
//void Categories::createDirectorySearchUrl ( Url  *url,
int32_t Categories::createDirSearchRequest ( char *requestBuf,
					  int32_t  requestBufSize,
					  int32_t  catid,
					  char *hostname,
					  int32_t  hostnameLen,
					  char *coll,
					  int32_t  collLen,
					  char *cgi,
					  int32_t  cgiLen,
					  bool  cgiFromRequest ,
					  HttpRequest *r ) {
	// setup the request Url
	//char buffer[1024+MAX_COLL_LEN];
	//int32_t bufferLen;
	//char *p    = buffer;
	char *p    = requestBuf;
	//char *pend = buffer + 1024+MAX_COLL_LEN;
	char *pend = requestBuf + requestBufSize;
	if ( p + (hostnameLen + collLen + 128 ) >= pend )
		return 0;
	// GET
	//p += sprintf(p, "GET ");
	// damnit, keep the ZET if that's what we had, that's how we know
	// if the sender requires a compressed reply (qcproxy = query 
	// compression proxy)
	char *cmd = "GET";
	char *rrr = r->m_reqBuf.getBufStart();
	if ( rrr && rrr[0] == 'Z' ) cmd = "ZET";
	// request
	//p += sprintf(p, "%s /search?dir=%"INT32"&dr=0&sc=0&sdir=%"INT32"&sdirt=0&c=",
	//		cmd, catid, catid);
	p += sprintf(p, 
		     "%s /search?q=gbcatid%%3A%"INT32"&dir=%"INT32"&dr=0&sc=0&c="
		     , cmd
		     , catid
		     , catid);
	// coll
	gbmemcpy(p, coll, collLen);
	p += collLen;
	// add extra cgi if we have it and have room
	if ( cgi && cgiLen > 0 && p + cgiLen + 76 < pend ) {
		// if it's from the request, need to add &'s and ='s
		if ( cgiFromRequest ) {
			//p += sprintf(p, "&");
			*p = '&'; p++;
			bool ampToggle = false;
			//for (int32_t i = cgiPos; i < cgiPos + cgiLen; i++) {
				//if ( p + 10 >= pend ) break;
			for (int32_t i = 0; i < cgiLen; i++) {
				//*p = decodedPath[i];
				*p = cgi[i];
				if (*p == '\0') {
					if (ampToggle) *p = '&';
					else           *p = '=';
					ampToggle = !ampToggle;
				}
				p++;
			}
		}
		else {
			gbmemcpy(p, cgi, cgiLen);
			p += cgiLen;
		}
	}
	// hostname
	p += sprintf(p, " HTTP/1.0\r\nHost: http://");
	gbmemcpy(p, hostname, hostnameLen);
	p += hostnameLen;
	// rest of the request
	p += sprintf(p, "\r\n"
			"Accept-Language: en\r\n"
			"Accept: text/html\r\n\r\n" );
	//buffer[p - buffer] = '\0';
	// set the Url
	//url->set(buffer, p - buffer);
	return p - requestBuf;
}

static HashTable langTables[MAX_LANGUAGES+1];

// Horrible hack, must fix later
bool Categories::loadLangTables(void) {
	char line[10240];
	FILE *content;
	uint32_t h;
	uint32_t lineno = 0L;
	uint32_t entries = 0L;
	char *cp;
	char *cpEnd = line + 10239;
	if(!(content = fopen("catdb/content.rdf.u8", "r"))) {
		log(LOG_INFO, "cat: could not open content file.\n");
		return(false);
	}

	while(!feof(content) &&
			fgets(line, 10239, content)) {
		lineno++;

		if(lineno % 1000000 == 0)
			log(LOG_INFO, "cat: Parsing line %"INT32"\n", lineno);

		if(!strncmp(line, "</ExternalPage>", 14)) {
			h = 0L; // end tag, clear hash
			continue;
		}

		if(!strncmp(line, "<ExternalPage about=\"", 21)) {
			cp = line + 28; // skip http:// too
			while(cp && *cp != '"' && cp < cpEnd)
				cp++;
			*cp = 0;
			h = hash32n(line + 28);
			continue;
		}

		if(h && !strncmp(line, "  <topic>Top/World/", 18)) {
			for(register int i = 2; i <= langTagalog; i++) {
				if(!memcmp(line + 19, langToTopic[i], 
					   gbstrlen((char *)langToTopic[i]))) {
					langTables[i].addKey(h, 1);
					entries++;
					h = 0; // paranoia, clear hash
				}
			}
		}
	}

	log(LOG_INFO, "cat: Added %"INT32" total entries.\n", entries);

	fclose(content);

	// Save all the tables for later
	for(register int i = 2; i <= langTagalog; i++) {
		sprintf(line, "catlang%03d.dat", i);
		langTables[i].save(g_hostdb.m_dir, line);
		if(langTables[i].getNumSlotsUsed() <= 0 ) {
			log(LOG_INFO, "cat: Don't seem to have any data in table %d\n", i);
		}
	}

	return(true);
}

bool Categories::initLangTables(void) {
	char name[512];
	register int i;
	// int64_t memory = g_mem.m_used;
	uint64_t start;
	uint64_t stop;
	for(i = 2; i <= MAX_LANGUAGES; i++) {

		// There is no language 5!
		if(i == 5) continue;

		/*
		langTables[i] = (HashTable *) mmalloc(sizeof(HashTable), "LangHashTable");
		if(!langTables[i]) {
			log(LOG_INFO,
			"cat: Could not allocate memory for category language tables.\n");
			return(false);
		}
		*/

		langTables[i].set(10); // paranoia
		snprintf(name, 511, "lang%03d.dat", i);
		langTables[i].load(g_hostdb.m_dir, name);
	}

	// check for any empty tables
	for(i = 2; i <= langTagalog; i++) {

		// There is no language 5!
		if(i == 5) continue;

		if(langTables[i].getNumSlotsUsed() <= 0 ) {
			log(LOG_INFO, "cat: Starting language load.\n");
			start = gettimeofdayInMicroseconds();
			loadLangTables();
			stop = gettimeofdayInMicroseconds();
			log(LOG_INFO,
					"cat: Parsing content took %"INT64" microseconds\n", stop - start);
			break;
		}
	}
	return(true);
}

uint8_t Categories::findLanguage(char *addr) {
	uint32_t h;
	char *cp = addr;
	if(!strncmp(cp, "http://", 7)) cp += 7;
	h = hash32(cp, gbstrlen(cp));
	for(register int i = 2; i <= langTagalog; i++) {
		if(i == 5) continue; // There is no language 5!
		if(langTables[i].getNumSlotsUsed() > 0 &&
			langTables[i].getSlot(h) >= 0)
			return((uint8_t)i);
	}
	return(0);
}

