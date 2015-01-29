#include "gb-include.h"

#include "matches2.h"
#include "Unicode.h"
#include "Titledb.h"
#include "HashTableT.h"

//make the key, it is just the needles ptr 
//static HashTableT<uint64_t , char*> s_quickTables;
static HashTableX s_quickTables;

/*
// returns false and sets g_errno on error
bool fast_highlight ( // highlight these query terms:
		      Query *q ,
		      // highlight query terms in this string
		      char *stringToHighlight ,
		      // store highlighted string here:
		      SafeBuf *dstBuf ) {


	// make them into needles first
	SafeBuf needles;
	int32_t need = q->m_numTerms * sizeof(Needle);
	if ( ! needles.reserve(need) ) return false;

	char *p = needles.getBufStart();
	for ( int32_t i = 0 ; i < q->m_numTerms ; i++ ) {
		QueryTerm *qt = &q->m_qterms[i];
		Needle *ne = (Needle *)p;
		p += sizeof(Needle);
		ne->m_string = qt->m_term;
		ne->m_stringSize = qt->m_termLen;
		ne->m_id = i;
		ne->m_isSection = false;
	}

	Needle *nbuf = needles.getBufStart();
	
	getMatches2 ( nbuf ,
		      q->m_numTerms,
		      stringToHighlight,
		      gbstrlen(stringToHighlight),
		      NULL, // linkpos
		      &needleNum,
		      false, // stopatfirstmatch?
		      NULL, // hadprematch?
		      true,// save quick tables?
		      niceness );

}
*/


// . get the first substring in "haystack" that matches a string in "needles"
// . set *needleNum to the # of the needle in "needles" that matched
// . return a ptr into "haystack" that matches, NULL if none
// . if "linkPos" is non-NULL then certain types of matches must occur
//   BEFORE "linkPos" in order to be counted. those certain types are indicated
//   by a Needle::m_section of 1. this is for allowing links before comment
//   sections to be able to vote, and links in/after comment section to not
//   vote. (see LinkText.cpp's call to getMatch() to better understand this)
// . ALL needles.m_strings MUST BE IN LOWER CASE!! otherwise, we should convert
//   to lower and store into tmp[]. TODO.
// . a space (includes \r \n) in a needle will match a consecutive sequence
//   of spaces in the haystack

#define BITVEC uint64_t

char *getMatches2 ( Needle *needles          , 
		    int32_t    numNeedles       ,
		    char   *haystack         , 
		    int32_t    haystackSize     ,
		    char   *linkPos          ,
		    int32_t   *needleNum        ,
		    bool    stopAtFirstMatch ,
		    bool   *hadPreMatch      ,
		    bool    saveQuickTables  ,
		    int32_t    niceness         ) {

	// assume not
	if ( hadPreMatch ) *hadPreMatch = false;
	// empty haystack? then no matches
	if ( ! haystack || haystackSize <= 0 ) return NULL;
	// JAB: no needles? then no matches
	if ( ! needles  || numNeedles   <= 0 ) return NULL;

	//char tmp[8192];
	//char *t    = tmp;
	//char *tend = tmp + 8192;

	// reset counts to 0
	//if ( ! stopAtFirstMatch )
	//	for ( int32_t i=0 ; i < numNeedles ; i++ ) 
	//		needles[i].m_count = 0;

	// are we responsible for init'ing string lengths? this is much
	// faster than having to specify lengths manually.
	for ( int32_t i=0 ; i < numNeedles; i++ ) {
		// breathe
		QUICKPOLL(niceness);
		// clear
		needles[i].m_count      = 0;
		needles[i].m_firstMatch = NULL;
		// set the string size in bytes if not provided
		if ( needles[i].m_stringSize == 0 )
			needles[i].m_stringSize = gbstrlen(needles[i].m_string);
	}

	// . set up the quick tables.
	// . utf16 is not as effective here because half the bytes are zeroes!
	// . TODO: use a static cache of like 4 of these tables where the key
	//         is the Needles ptr ... done
	int32_t numNeedlesToInit = numNeedles;
	char space[256 * 4 * sizeof(BITVEC)];
	char *buf = NULL;

	BITVEC *s0;
	BITVEC *s1;
	BITVEC *s2;
	BITVEC *s3;

	/*
	static bool s_quickTableInit = false;
	static char s_qtbuf[128*(12+1)*2];

	int32_t slot = -1;
	if(saveQuickTables) {
		if ( ! s_quickTableInit ) {
			s_quickTableInit = true;
			s_quickTables.set(8,4,128,s_qtbuf,256*13,false,0,"qx");
		}
		uint64_t key = (uint32_t)needles;
		slot = s_quickTables.getSlot(&key);
		if ( slot >= 0 ) {
			buf = s_quickTables.getValueFromSlot(slot);
			numNeedlesToInit = 0;
		}
	}
	*/

	if(!buf) {
		buf = space;
		memset ( buf , 0 , sizeof(BITVEC)*256*4);
	}

	/*
	if( useQuickTables && slot == -1 ) {
		//buf = (char*)mcalloc(sizeof(uint32_t)*256*5,
		//		     "matches");
		if(buf) s_quickTables.addKey(&key, &buf);
		//sanity check, no reason why there needs to be a 
		//limit, I just don't expect there to be this many
		//static needles at this point.
		if(s_quickTables.getNumSlotsUsed() > 32){
			char *xx=NULL; *xx = 0;
		}
	}
	*/

	// try 64 bit bit vectors now since we doubled # of needles
	int32_t offset = 0;
	s0 = (BITVEC *)(buf + offset);
	offset += sizeof(BITVEC)*256;
	s1 = (BITVEC *)(buf + offset);
	offset += sizeof(BITVEC)*256;
	s2 = (BITVEC *)(buf + offset);
	offset += sizeof(BITVEC)*256;
	s3 = (BITVEC *)(buf + offset);
	offset += sizeof(BITVEC)*256;

	BITVEC mask;

	// set the letter tables, s0[] through sN[], for each needle
	for ( int32_t i = 0 ; i < numNeedlesToInit ; i++ ) {
		// breathe
		QUICKPOLL(niceness);
		unsigned char *w    = (unsigned char *)needles[i].m_string;
		unsigned char *wend = w + needles[i].m_stringSize;
		// BITVEC is now 64 bits
		mask = (1<<(i&0x3f)); // (1<<(i%64));
		// if the needle is small, fill up the remaining letter tables
		// with its mask... so it matches any character in haystack.
		s0[(unsigned char)to_lower_a(*w)] |= mask;
		s0[(unsigned char)to_upper_a(*w)] |= mask;
		w += 1;//step;
		if ( w >= wend ) {
			for ( int32_t j = 0 ; j < 256 ; j++ )  {
				s1[j] |= mask;
				s2[j] |= mask;
				s3[j] |= mask;
			}
			continue;
		}

		s1[(unsigned char)to_lower_a(*w)] |= mask;
		s1[(unsigned char)to_upper_a(*w)] |= mask;
		w += 1;//step;
		if ( w >= wend ) {
			for ( int32_t j = 0 ; j < 256 ; j++ )  {
				s2[j] |= mask;
				s3[j] |= mask;
			}
			continue;
		}

		s2[(unsigned char)to_lower_a(*w)] |= mask;
		s2[(unsigned char)to_upper_a(*w)] |= mask;
		w += 1;//step;
		if ( w >= wend ) {
			for ( int32_t j = 0 ; j < 256 ; j++ )  {
				s3[j] |= mask;
			}
			continue;
		}

		s3[(unsigned char)to_lower_a(*w)] |= mask;
		s3[(unsigned char)to_upper_a(*w)] |= mask;
		w += 1;//step;
	}

	// return a ptr to the first match if we should, this is it
	char *retVal = NULL;
	// debug vars
	//int32_t debugCount = 0;
	//int32_t pp = 0;
	// now find the first needle in the haystack
	unsigned char *p    = (unsigned char *)haystack;
	unsigned char *pend = (unsigned char *)haystack + haystackSize;
	char          *dend = (char *)pend;

	// do not breach!
	pend -= 4;

	for ( ; p < pend ; p++ ) {
		// breathe
		QUICKPOLL(niceness);
		//if ( (char *)p - (char *)haystack >= 12508 )
		//	log("hey");
		// analytics...
		
		// is this a possible match? (this should be VERY fast)
		mask  = s0[*(p+0)];
		if ( ! mask ) continue;
		mask &= s1[*(p+1)];
		if ( ! mask ) continue;
		mask &= s2[*(p+2)];
		if ( ! mask ) continue;
		mask &= s3[*(p+3)];
		if ( ! mask ) continue;
		//debugCount++;
		/*
		// display
		char oo[148];
		char *xx ;
		xx = oo;
		//gbmemcpy ( xx , p , 8 );
		for ( int32_t k = 0 ; k < 5 ; k++ ) {
			*xx++ = p[k];
		}
		gbmemcpy ( xx , "..." , 3 );
		xx += 3;
		*/
		//
		// XXX: do a hashtable lookup here so we have the candidate
		//      matches in a chain... 
		// XXX: for small needles which match frequently let's have
		//      a single char hash table, a 2 byte char hash table,
		//      etc. so if we have small needles we check the hash
		//      in those tables first, but only if mask & SMALL_NEEDLE
		//      is true! the single byte needle hash table can just
		//      be a lookup table. just XOR the bytes together for
		//      the hash.
		// XXX: just hash the mask into a table to get candidate
		//      matches in a chain? but there's 4B hashes!!
		// we got a good candidate, loop through all the needles
		for ( int32_t j = 0 ; j < numNeedles ; j++ ) {
			// skip if does not match mask, will save time
			if ( ! ((1<<(j&0x3f)) & mask) ) continue;
			if( needles[j].m_stringSize > 3) {
				// ensure first 4 bytes matches this needle's
				if (needles[j].m_string[0]!=to_lower_a(*(p+0)))
					continue;
				if (needles[j].m_string[1]!=to_lower_a(*(p+1)))
					continue;
				if (needles[j].m_string[2]!=to_lower_a(*(p+2)))
					continue;
				if (needles[j].m_string[3]!=to_lower_a(*(p+3)))
					continue;
			}
			// get needle size
			int32_t msize = needles[j].m_stringSize;
			// can p possibly be big enough?
			if ( pend - p < msize ) continue;
			// needle is "m" now
			char *m    = needles[j].m_string;
			char *mend = needles[j].m_stringSize + m;
			// use a tmp ptr for ptr into haystack
			char *d = (char *)p;
			// skip first 4 bytes since we know they match
			if(msize > 3) {
				d += 4;
				m += 4;
			}
			// loop over each char in "m"
			//for ( ; *m ; m++ ) {
			for ( ; m < mend ; m++ ) {
				//while ( ! *d && d < dend ) d++;
				//while ( ! *m && m < mend ) m++;
				// if we are a non alnum, that will match
				// any string of non-alnums, like a space
				// for instance. the 0 byte does not count
				// because it is used in utf16 a lot. this
				// may trigger some false matches in utf16
				// but, oh well... this way "link partner"
				// will match "link  - partner" in the haystk
				if ( is_wspace_a(*m) && m < mend ) {
					// skip all in "d" then.
					while (d<dend&&is_wspace_a(*d)) d++;
					// advance m then
					continue;
				}
				// make sure we match otherwise
				if ( *m != to_lower_a(*d) ) break;
				// ok, we matched, go to next
				d++;
			}
			// if not null, keep going
			if ( m < mend ) continue;
			// if this needle is "special" AND it occurs AFTER
			// linkPos, then do not consider it a match. this is
			// if we have a comment section indicator, like
			// "div id=\"comment" AND it occurs AFTER linkPos
			// (the char ptr to our link in the haystack) then
			// the match does not count.
			if ( linkPos && needles[j].m_isSection && 
			     (char *)p>linkPos ) {
				// record this for LinkText.cpp
				if ( hadPreMatch ) *hadPreMatch = true;
				continue;
			}
			// store ptr if NULL
			if ( ! needles[j].m_firstMatch )
				needles[j].m_firstMatch = (char *)p;
			// return ptr to needle in "haystack"
			if ( stopAtFirstMatch ) {
				// ok, we got a match
				if ( needleNum ) *needleNum = j;
				//return (char *)p;
				retVal = (char *)p;
				p = pend;
				break;
			}
			// otherwise, just count it
			needles[j].m_count++;
			// see if we match another needle, fixes bug
			// of matching "anal" but not "analy[tics]"
			continue;
			// advance to next char in the haystack
			break;
		}
		// ok, we did not match any needles, advance p and try again
	}

	//
	// HACK:
	// 
	// repeat above loop but for the last 4 characters in haystack!!
	// this fixes a electric fence mem breach core
	//
	// it is slower because we check for \0
	//
	pend += 4;

	for ( ; p < pend ; p++ ) {
		// breathe
		QUICKPOLL(niceness);
		//if ( (char *)p - (char *)haystack >= 12508 )
		//	log("hey");
		// is this a possible match? (this should be VERY fast)
		mask  = s0[*(p+0)];
		if ( ! mask ) continue;
		if ( p+1 < pend ) {
			mask &= s1[*(p+1)];
			if ( ! mask ) continue;
		}
		if ( p+2 < pend ) {
			mask &= s2[*(p+2)];
			if ( ! mask ) continue;
		}
		if ( p+3 < pend ) {
			mask &= s3[*(p+3)];
			if ( ! mask ) continue;
		}
		//debugCount++;
		/*
		// display
		char oo[148];
		char *xx ;
		xx = oo;
		//gbmemcpy ( xx , p , 8 );
		for ( int32_t k = 0 ; k < 5 ; k++ ) {
			*xx++ = p[k];
		}
		gbmemcpy ( xx , "..." , 3 );
		xx += 3;
		*/
		//
		// XXX: do a hashtable lookup here so we have the candidate
		//      matches in a chain... 
		// XXX: for small needles which match frequently let's have
		//      a single char hash table, a 2 byte char hash table,
		//      etc. so if we have small needles we check the hash
		//      in those tables first, but only if mask & SMALL_NEEDLE
		//      is true! the single byte needle hash table can just
		//      be a lookup table. just XOR the bytes together for
		//      the hash.
		// XXX: just hash the mask into a table to get candidate
		//      matches in a chain? but there's 4B hashes!!
		// we got a good candidate, loop through all the needles
		for ( int32_t j = 0 ; j < numNeedles ; j++ ) {
			// skip if does not match mask, will save time
			if ( ! ((1<<(j&0x3f)) & mask) ) continue;
			if( needles[j].m_stringSize > 3) {
				// ensure first 4 bytes matches this needle's
				if (needles[j].m_string[0]!=to_lower_a(*(p+0)))
					continue;
				if (!p[1] ||
				    needles[j].m_string[1]!=to_lower_a(*(p+1)))
					continue;
				if (!p[2] ||
				    needles[j].m_string[2]!=to_lower_a(*(p+2)))
					continue;
				if (!p[3] ||
				    needles[j].m_string[3]!=to_lower_a(*(p+3)))
					continue;
			}
			// get needle size
			int32_t msize = needles[j].m_stringSize;
			// can p possibly be big enough?
			if ( pend - p < msize ) continue;
			// needle is "m" now
			char *m    = needles[j].m_string;
			char *mend = needles[j].m_stringSize + m;
			// use a tmp ptr for ptr into haystack
			char *d = (char *)p;
			// skip first 4 bytes since we know they match
			if(msize > 3) {
				d += 4;
				m += 4;
			}
			// loop over each char in "m"
			//for ( ; *m ; m++ ) {
			for ( ; m < mend ; m++ ) {
				//while ( ! *d && d < dend ) d++;
				//while ( ! *m && m < mend ) m++;
				// if we are a non alnum, that will match
				// any string of non-alnums, like a space
				// for instance. the 0 byte does not count
				// because it is used in utf16 a lot. this
				// may trigger some false matches in utf16
				// but, oh well... this way "link partner"
				// will match "link  - partner" in the haystk
				if ( is_wspace_a(*m) && m < mend ) {
					// skip all in "d" then.
					while (d<dend&&is_wspace_a(*d)) d++;
					// advance m then
					continue;
				}
				// make sure we match otherwise
				if ( *m != to_lower_a(*d) ) break;
				// ok, we matched, go to next
				d++;
			}
			// if not null, keep going
			if ( m < mend ) continue;
			// if this needle is "special" AND it occurs AFTER
			// linkPos, then do not consider it a match. this is
			// if we have a comment section indicator, like
			// "div id=\"comment" AND it occurs AFTER linkPos
			// (the char ptr to our link in the haystack) then
			// the match does not count.
			if ( linkPos && needles[j].m_isSection && 
			     (char *)p>linkPos ) {
				// record this for LinkText.cpp
				if ( hadPreMatch ) *hadPreMatch = true;
				continue;
			}
			// store ptr if NULL
			if ( ! needles[j].m_firstMatch )
				needles[j].m_firstMatch = (char *)p;
			// return ptr to needle in "haystack"
			if ( stopAtFirstMatch ) {
				// ok, we got a match
				if ( needleNum ) *needleNum = j;
				//return (char *)p;
				retVal = (char *)p;
				p = pend;
				break;
			}
			// otherwise, just count it
			needles[j].m_count++;
			// advance to next char in the haystack
			break;
		}
		// ok, we did not match any needles, advance p and try again
	}


	//if ( debugCount > 0 ) pp = haystackSize / debugCount;
	//log("build: debug count = %"INT32" uc=%"INT32" hsize=%"INT32" "
	//    "1 in %"INT32" chars matches.",
	//    debugCount,(int32_t)isHaystackUtf16,haystackSize,pp);

	// before we exit, clean up
	return retVal;
}
