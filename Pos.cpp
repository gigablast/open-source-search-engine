#include "gb-include.h"

#include "Pos.h"
#include "Sections.h"

Pos::Pos() {
	m_buf = NULL;
	m_needsFree = false;
}

Pos::~Pos () {
	reset();
}

void Pos::reset() {
	if ( m_buf && m_needsFree )
		mfree ( m_buf , m_bufSize , "Pos" );
	m_buf = NULL;
}	

// . the interval is half-open [a,b)
// . do not print out any alnum word with negative score
int32_t Pos::filter( char *p, char *pend, class Words *words, int32_t a, 
		  int32_t b, Sections *sections ) {
	int32_t plen = 0;
	set ( words , sections , p , pend, &plen , a , b );
	return plen;
}

// . set the filtered position of each word
// . used by Summary.cpp to determine how many chars are in the summary,
//   be those chars single byte or utf8 chars that are 4 bytes 
// . returns false and sets g_errno on error
// . if f is non-NULL store filtered words into there. back to back spaces
//   are eliminated.
bool Pos::set ( Words  *words  ,
		Sections *sections ,
		char   *f   ,
		char   *fend,
		int32_t   *len ,
		int32_t    a   , 
		int32_t    b   ,
		char   *buf ,
		int32_t    bufSize ) {

	// free m_buf in case this is a second call
	if ( ! f ) reset();

	int32_t        nw    = words->getNumWords();
	int32_t       *wlens = words->m_wordLens;
	nodeid_t   *tids  = words->getTagIds(); // m_tagIds;
	char      **wp    = words->m_words;
	//int32_t       *ss    = NULL;
	//int64_t  *wids  = words->m_wordIds;
	//if ( scores ) ss  = scores->m_scores;

	// save start point for filtering
	char *fstart = f;

	// -1 is the default value
	if ( b == -1 ) b = nw;

	// alloc array if need to
	int32_t need = (nw+1) * 4;

	// do not destroy m_pos/m_numWords if only filtering into a buffer
	if ( f ) goto skip;

	m_needsFree = false;

	m_buf = m_localBuf;
	if ( need > POS_LOCALBUFSIZE && need < bufSize ) 
		m_buf = buf;
	else if ( need > POS_LOCALBUFSIZE ) {
		m_buf = (char *)mmalloc(need,"Pos");
		m_needsFree = true;
	}
	// bail on error
	if ( ! m_buf ) return false;
	m_bufSize = need;
	m_pos      = (int32_t *)m_buf;
	m_numWords = nw;

 skip:
	// this is the CHARACTER count. 
	int32_t pos = 0;
	bool trunc = false;
	char *p , *pend;
	//char *nextp;
	//int32_t  skip;

	char* lastBreak = NULL;
	// utf8 char
	//int32_t c;
	// its size in bytes
	//char cs;

	// int16_tcut
	//Section **sp = NULL;
	//if ( sections ) sp = sections->m_sectionPtrs;

	//int32_t badFlags = SEC_SCRIPT|SEC_STYLE|SEC_SELECT|SEC_MARQUEE;

	// flag for stopping back-to-back spaces. only count those as one char.
	bool lastSpace = false;
 	int32_t maxCharSize = 4; // we are utf8 
	for ( int32_t i = a ; i < b ; i++ ) {
		if (trunc) break;
		// set pos for the ith word to "pos"
		if ( ! f ) m_pos[i] = pos;

		// if inside a bad tag, skip it
		//if ( sp && (sp[i]->m_flags & badFlags) ) continue;

		// is tag?
		if ( tids && tids[i] ) {
			// if not breaking, does nothing
			if ( ! g_nodes[tids[i]&0x7f].m_isBreaking ) continue;
			// list tag? <li>
			if ( tids[i] == TAG_LI ) { 
				if ( f ){
					if ((fend - f > maxCharSize)) {
						*f++ = '*';
					}
					else {
						trunc = true;
					}
				}
				pos++;
				lastSpace = false;
				continue;
			}
			// if had a previous breaking tag and no non-tag
			// word after it, do not count back-to-back spaces
			if ( lastSpace ) continue;
			// if had a br tag count it as a '.'
			if ( tids[i] ) { // == 20 ) { // <br> 
				// are we filtering?
				if ( f && f != fstart ) {
					if ((fend-f>2*maxCharSize)) {
						*f++ = '.';
						*f++ = ' ';
					}
					else trunc = true;
				}
				// count as double periods
				//pos += 3;
				// no, just single period.
				pos += 2;
				lastSpace = true;
				continue;
			}
			// are we filtering?
			if ( f ) {
				if ((fend-f > maxCharSize)) {
					*f++ = ' ';
				}
				else trunc = true;
			}
			// count as a single space
			pos++;
			// do not allow back-to-back spaces
			lastSpace = true;
			continue;
		}
		
		// scan through all chars discounting back-to-back spaces
		
		// assume filters out to the same # of chars
		p    = wp[i] ;
		pend = p + wlens[i];
		unsigned char cs = 0;
		for ( ; p < pend ; p += cs ) {
			// get size
			cs = getUtf8CharSize(p);
			// do not count space if one before
			if ( is_wspace_utf8 (p) ) {
				if ( lastSpace ) continue;
				lastSpace = true;
				// are we filtering?
				if ( f ) {
					if (fend-f > 1 ) {
						lastBreak = f;
						*f++ = ' ';
					}
					else trunc = true;
				}
				pos++;
				continue;
			}
			if ( f ) {
				if (fend-f > cs){
					// change '|' to commas
					if ( *p == '|' )
						*f++ = ',';
					else if ( cs == 1 )
						*f++ = *p;
					else {
						gbmemcpy(f,p,cs);
						f += cs;
					}
				}
				else trunc = true;
			}

			pos++; 
			lastSpace = false;
		}
	}
	if (trunc) {
		if(lastBreak == NULL) {
			*len = 0;
			return false;
 		}
 		else if(f) f = lastBreak;
	}
	// set pos for the END of the last word here (used in Summary.cpp)
	if ( ! f ) m_pos[nw] = pos;
	// NULL terminate f
	else { *len = f - fstart; }
	if ( fend-f > maxCharSize) { *f = '\0';}
	// Success
	return true;
}
