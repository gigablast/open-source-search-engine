#include "gb-include.h"

#include "HashTableX.h"
#include "Unicode.h"
// based on http://www.unicode.org/reports/tr15/Normalizer.java

//static int32_t decompose(UChar *outBuf, int32_t outBufSize, 
//		      UChar *inBuf, int32_t inBufSize, bool decodeEntities) ;
//static int32_t compose(UChar *buf, int32_t bufSize, bool strip) ;
//static UChar32 ucGetComposition(UChar32 c1, UChar32 c2) ;

/*
int32_t ucNormalizeNFKC(UChar *outBuf, int32_t outBufSize,
		     UChar *inBuf, int32_t inBufSize, bool strip) {
#if 0
	UChar *p = inBuf;
	UChar *q = NULL;
	UChar *i = outBuf;
	while(p < inBuf+inBufSize) {
		UChar32 c = utf16EntityDecode(p, &q);
		if (ucProperties(c) & UC_NFKC_QC_NO) break;
		//printf("[U+%04X]: NFKC ok\n", c);
		if (i >= outBuf+outBufSize){
			log("Out of space for normalization!");
			return i-outBuf;
		}
		i += utf16Encode(c,i);
		p = q;
	}
	if (p >= inBuf+inBufSize) {
		//printf("skipping normalization\n");
		return i-outBuf;
	}
	//printf("p: %d\n", p - inBuf);
	if (p > inBuf) utf16Prev(p, &p);
	if (i > outBuf) utf16Prev(i, &i);
	//printf("new p: %d\n", p - inBuf);
	int32_t plen = p - inBuf + inBufSize; 
	int32_t ilen = i - outBuf + outBufSize; 
	//log("UCNormalizer: Actually normalizing %"INT32" chars", inBufSize);
	int32_t decompLen = decompose(i, ilen, p, plen);
#endif
	int32_t decompLen = decompose(outBuf, outBufSize, inBuf, inBufSize, true);
	return compose(outBuf, decompLen, strip);
}

int32_t ucNormalizeNFKD(UChar *outBuf, int32_t outBufSize,
		     UChar *inBuf, int32_t inBufSize) {

	int32_t decompLen = decompose(outBuf, outBufSize, inBuf, inBufSize, true);
	return decompLen;
}

static int32_t decompose(UChar *outBuf, int32_t outBufSize, 
		      UChar *inBuf, int32_t inBufSize, bool decodeEntities) {

	UChar *p = inBuf;
	UChar *q = outBuf;
	while (p < inBuf+inBufSize) {
		UChar32 c;
		if(decodeEntities)
			c = utf16EntityDecode(p, &p);
		else
			c = utf16Decode(p, &p);
		UChar32 decomp[32];
		int32_t decompLen = recursiveKDExpand(c, decomp, 32);
		for (int i=0 ; i < decompLen && (q<outBuf+outBufSize); i++) {
			UChar32 d = decomp[i];
			unsigned char cc = ucCombiningClass(d);
			// fix out-of-order combining chars
			// Gah...this shouldn't happen too often
			if (cc) {
				UChar *qq = q; //insert point
				UChar32 c2;
				while (qq  > outBuf){
					UChar *qprev;
					c2 = utf16Prev(qq, &qprev);
					if (ucCombiningClass(c2) <= cc) break;
					qq = qprev;
				}
				if (qq < q){ // move chars out of the way
					int32_t cSize = utf16Size(c);
					memmove(qq+cSize, qq, (q-qq) << 1 );
				}
				q += utf16Encode(d, qq);
			}
			else
				q += utf16Encode(d, q);
		}
	}
	return q - outBuf;
}

static int32_t compose(UChar *buf, int32_t bufSize, bool strip) {
	UChar *p = buf; // read cursor
	UChar *s = buf; //starter position
	if (!buf || !bufSize) return 0;
	UChar32 starterCP = utf16Decode(p, &p);
	UChar *q = p; // write cursor
	unsigned char lastCC = ucCombiningClass(starterCP);
	while (p < buf+bufSize) {
		UChar32 c = utf16Decode(p,&p);
		unsigned char cc = ucCombiningClass(c);

		// skip combining characters if base char is a latin letter
		if (strip && cc && !(starterCP & 0xffffff80) ) continue;

		UChar32 composite = ucGetComposition(starterCP, c);
		if (composite && (lastCC < cc || lastCC == 0)) {
			utf16Encode(composite, s);
			starterCP = composite;
		}
		else{
			if (cc == 0) {
				s = q;
				starterCP = c;
			}
			lastCC = cc;
			q += utf16Encode(c, q);
		}
	}
	return q - buf;
}
*/

static bool s_isInitialized = false;
static HashTableX s_compositions;
#define COMPBUFSIZE 64*1024*20
static char s_compBuf[COMPBUFSIZE];

// Kompatible Decomposition table must be loaded before calling this
bool initCompositionTable(){
	if ( ! s_isInitialized ) {
		//log(LOG_INFO,"conf: UCNormalizer: "
		//    "initializing Full Composition table");
		// set up the hash table
		//if ( ! s_compositions.set ( 8,4,16384 ) )
		if (!s_compositions.set(8,4,65536,s_compBuf,(int32_t)COMPBUFSIZE,
					false,0,"uccomptbl" ))
			return log("conf: Could not init table of "
				   "HTML entities.");
		// now add in all the composition pairs
		for (int32_t i = 0; i < 0xF0000; i++) {
			int32_t mapCount;
			bool fullComp;
			UChar32 *map = getKDValue(i, &mapCount, &fullComp);
			//printf("U+%04x fullComp: %s\n",
			//      i, fullComp?"true":"false");
			if (!fullComp) continue;
			// all full compositions have exactly 2 decomps
			if (mapCount != 2) {
				//return
				log(LOG_WARN, "conf: "
				    "UCNormalizer: bad canonical "
				    "decomposition for %04"XINT32" (count: %"INT32")", 
				    i, mapCount);
				continue;
			}
			
			int64_t h = ((int64_t)map[0] << 32) | map[1];
			//if ( ! s_compositions.addTerm( &h, i) ) 
			//	return log("conf: bad init comp table");
			if ( ! s_compositions.addKey ( &h, &i) ) 
				return log("conf: bad init comp table");
		}
		s_isInitialized = true;
	} 
	return true;
}

/*
static UChar32 ucGetComposition(UChar32 c1, UChar32 c2) {
	int64_t h = ((int64_t)c1 << 32) | c2;
	return (UChar32) s_compositions.getScoreFromTermId( h );
}
*/

void resetCompositionTable() {
	s_compositions.reset();
	s_isInitialized = false;
}
