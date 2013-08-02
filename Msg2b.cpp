#include "gb-include.h"

#include "Msg2b.h"
#include "sort.h"

static char *gCatBuffer;
static int   sortSubCats ( const void *c1, const void *c2 );

Msg2b::Msg2b ( ) {
	m_subCats       = NULL;
	m_subCatsSize   = 0;
	m_numSubCats    = 0;
	m_catBuffer     = NULL;
	m_catBufferSize = 0;
	m_catBufferLen  = 0;
}

Msg2b::~Msg2b ( ) {
	// if sizes are 0, we don't own the data
	// otherwise free it up
	if (m_subCats && m_subCatsSize > 0)
		mfree(m_subCats, m_subCatsSize, "Msg2b");
	if (m_catBuffer && m_catBufferSize > 0)
		mfree(m_catBuffer, m_catBufferSize, "Msg2b");
}

//
// Generate sorted sub categories in m_subCats Array
// m_catBuffer stores sub category names and prefixes
//
bool Msg2b::generateDirectory ( long   dirId,
				void  *state,
				void (*callback)(void *state) ) {
	m_dirId    = dirId;
	m_st       = state;
	m_callback = callback;
	// sub categories buffer
	m_subCatsSize = MAX_SUB_CATS * sizeof(SubCategory);
	m_numSubCats  = 0;
	m_subCats = (SubCategory*)mmalloc(m_subCatsSize, "Msg2b");
	if (!m_subCats) {
		log("Msg2b: Could not allocate %li bytes for m_subCats.",
		    m_subCatsSize);
		g_errno = ENOMEM;
		return true;
	}
	// name buffer
	m_catBufferSize = 4096;
	m_catBufferLen  = 0;
	m_catBuffer = (char*)mmalloc(m_catBufferSize, "PageResults");
	if (!m_catBuffer) {
		log("Msg2b: Could not allocate %li bytes for m_catBuffer.",
		    m_catBufferSize);
		g_errno = ENOMEM;
		return true;
	}
	// generate the sub categories
	m_numSubCats = g_categories->generateSubCats (  m_dirId,
						       m_subCats,
						      &m_catBuffer,
						      &m_catBufferSize,
						      &m_catBufferLen );
	// sort the categories by type and prefix
	gCatBuffer = m_catBuffer;
	gbsort(m_subCats, m_numSubCats, sizeof(SubCategory), sortSubCats);

	return true;
}

// sort categories by type and name
int sortSubCats ( const void *c1, const void *c2 ) {
	SubCategory *subCat1 = (SubCategory*)c1;
	SubCategory *subCat2 = (SubCategory*)c2;
	// check for a type difference of more than 10
	if (subCat1->m_type - subCat2->m_type <= -10)
		return -1;
	else if (subCat1->m_type - subCat2->m_type >= 10)
		return 1;
	// otherwise compare by prefix
	long preLen;
	if (subCat1->m_prefixLen < subCat2->m_prefixLen)
		preLen = subCat1->m_prefixLen;
	else
		preLen = subCat2->m_prefixLen;
	long preCmp = strncasecmp(&gCatBuffer[subCat1->m_prefixOffset],
				  &gCatBuffer[subCat2->m_prefixOffset],
				  preLen);
	// if equal, shorter is less
	if (preCmp == 0)
		return (subCat1->m_prefixLen - subCat2->m_prefixLen);
	else
		return preCmp;
}

//
// Serialize/Deserialize functions
//
long Msg2b::getStoredSize ( ) {
	return ( sizeof(long)*3 + // m_dirId + m_numSubCats + m_catBufferLen
		 sizeof(SubCategory) * m_numSubCats + // sub cats
		 m_catBufferLen ); // cat buffer
}

long Msg2b::serialize ( char *buf, long bufLen ) {
	// make sure we have room
	long storedSize = getStoredSize();
	if (bufLen < storedSize)
		return -1;
	char *p = buf;
	// m_dirId + m_numSubCats + m_catBufferLen
	*(long *)p = m_dirId;        p += sizeof(long);
	*(long *)p = m_numSubCats;   p += sizeof(long);
	*(long *)p = m_catBufferLen; p += sizeof(long);
	// sub cats
	memcpy(p, m_subCats, sizeof(SubCategory)*m_numSubCats);
	p += sizeof(SubCategory)*m_numSubCats;
	// cat buffer
	memcpy(p, m_catBuffer, m_catBufferLen);
	p += m_catBufferLen;
	// sanity check
	if (p - buf != storedSize) {
		log("Msg2b: Bad serialize size, %i != %li, bad engineer.",
		    p - buf, storedSize);
		char *xx = NULL; *xx = 0;
	}
	// return bytes stored
	return storedSize;
}

long Msg2b::deserialize ( char *buf, long bufLen ) {
	char *p = buf;
	if ( bufLen < (long)sizeof(long)*3 )
		return -1;
	// m_dirId + m_numSubCats + m_catBufferLen
	m_dirId        = *(long *)p; p += sizeof(long);
	m_numSubCats   = *(long *)p; p += sizeof(long);
	m_catBufferLen = *(long *)p; p += sizeof(long);
	if ( bufLen < (long)sizeof(long)*3 +
		      (long)sizeof(SubCategory)*m_numSubCats +
		      m_catBufferLen )
		return -1;
	// sub cats
	m_subCatsSize = 0; // this makes sure we don't free it
	m_subCats = (SubCategory*)p;
	p += sizeof(SubCategory)*m_numSubCats;
	// cat buffer
	m_catBufferSize = 0; // again, don't free
	m_catBuffer = p;
	p += m_catBufferLen;
	// sanity check
	if (p - buf > bufLen) {
		log("Msg2b: Overstepped deserialize buffer length, "
		    "%i > %li, bad engineer.",
		    p - buf, bufLen);
		char *xx = NULL; *xx = 0;
	}
	// return bytes stored
	return p - buf;
}
