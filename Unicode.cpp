#include "gb-include.h"

#include "UCNormalizer.h"

#include "Mem.h"
#include "HashTable.h"
#include "iana_charset.h"
#include "Titledb.h"

static HashTableX s_convTable;
// JAB: warning abatement
//static bool verifyIconvFiles();
static bool openIconvDescriptors() ;
// alias iconv_open and close to keep count of usage
// and prevent leaks..
// now just cache all iconvs in a hash table
// static iconv_t gbiconv_open(const char *tocode, const char *fromcode) ;
// static int gbiconv_close(iconv_t cd) ;


iconv_t gbiconv_open( char *tocode, char *fromcode) {
	// get hash for to/from
	uint32_t hash1 = hash32Lower_a(tocode, gbstrlen(tocode), 0);
	uint32_t hash2 = hash32Lower_a(fromcode, gbstrlen(fromcode),0);
	uint32_t hash = hash32h(hash1, hash2);

	g_errno = 0;
	iconv_t *convp = (iconv_t *)s_convTable.getValue(&hash);
	iconv_t conv = NULL;
	if ( convp ) conv = *convp;
	//log(LOG_DEBUG, "uni: convertor %s -> %s from hash 0x%"XINT32": 0x%"XINT32"",
	//    fromcode, tocode,
	//    hash, conv);
	if (!conv){
		//log(LOG_DEBUG, "uni: Allocating new convertor for "
		//    "%s to %s (hash: 0x%"XINT32")",
		//    fromcode, tocode,hash);
		conv = iconv_open(tocode, fromcode);
		if (conv == (iconv_t) -1) {
			log(LOG_WARN, "uni: failed to open converter for "
			    "%s to %s: %s (%d)", fromcode, tocode, 
			    strerror(errno), errno);
			// need to stop if necessary converters don't open
			//char *xx=NULL; *xx = 0;
			g_errno = errno;
			if (errno == EINVAL)
				g_errno = EBADCHARSET;
			
			return conv;
		}
		// add mem to table to keep track
		g_mem.addMem((void*)conv, 52, "iconv", 1);
		// cache convertor
		s_convTable.addKey(&hash, &conv);
		//log(LOG_DEBUG, "uni: Saved convertor 0x%"INT32" under hash 0x%"XINT32"",
		//    conv, hash);
	}
	else{
		// reset convertor
		char *dummy = NULL;
		size_t dummy2 = 0;
		// JAB: warning abatement
		//size_t res = iconv(conv,NULL,NULL,&dummy,&dummy2);
		iconv(conv,NULL,NULL,&dummy,&dummy2);
	}

	return conv;
}

int gbiconv_close(iconv_t cd) {
	//int val = iconv_close(cd);
	//if (val  == 0) g_mem.rmMem((void*)cd, 1, "iconv", 1);
	//return val;	
	return 0;
}

void gbiconv_reset(){
	for (int32_t i=0;i<s_convTable.getNumSlots();i++){
		//int32_t key = *(int32_t *)s_convTable.getKey(i);
		//if (!key) continue;
		if ( ! s_convTable.m_flags[i] ) continue;
		iconv_t *pconv = (iconv_t *)s_convTable.getValueFromSlot(i);
		if (! pconv) continue;
		iconv_t iconv = *pconv;
		//logf(LOG_DEBUG, "iconv: freeing iconv: 0x%x", (int)iconv);
		g_mem.rmMem((void*)iconv, 52, "iconv");
		libiconv_close(iconv);
	}
	s_convTable.reset();
}

#undef iconv_open
#define iconv_open(to, from) ((iconv_t)coreme(0))
#undef iconv_close
#define iconv_close(cd) ((int)coreme(0))




#define MAX_BAD_CHARS 500

#define VERIFY_UNICODE_CHECKSUMS 1

#define CHKSUM_UPPERMAP          1241336150
#define CHKSUM_LOWERMAP          1023166806
#define CHKSUM_PROPERTIES        33375957
#define CHKSUM_COMBININGCLASS    526097805
#define CHKSUM_SCRIPTS           1826246000
#define CHKSUM_KDMAP             1920116453

bool ucInit(char *path, bool verifyFiles){

	char file[384];
	if (path == NULL) path = "./";

	// Might want to move this out of ucInit someday
	// but right now it's the only thing that uses .so files (?)
	char gbLibDir[512];
	snprintf(gbLibDir, 512, "%s/lib",path);
	// i don't think this is used any more because we don't have it!
	//log(LOG_INIT, "ucinit: Setting LD_RUN_PATH to \"%s\"",gbLibDir);
	if (setenv("LD_RUN_PATH", gbLibDir, 1)){
		log(LOG_INIT, "Failed to set LD_RUN_PATH");
	}
	//char *ldpath = getenv("LD_RUN_PATH");
	// i don't think this is used any more because we don't have it!
	//log(LOG_DEBUG, "ucinit: LD_RUN_PATH: %s\n", ldpath);


	strcpy(file, path);
	strcat(file, "/ucdata/uppermap.dat");
	if (!loadUnicodeTable(&g_ucUpperMap,file, 
			      VERIFY_UNICODE_CHECKSUMS, 
			      CHKSUM_UPPERMAP))
		goto failed;
	strcpy(file, path);
	strcat(file, "/ucdata/lowermap.dat");
	if (!loadUnicodeTable(&g_ucLowerMap,file, 
			      VERIFY_UNICODE_CHECKSUMS, 
			      CHKSUM_LOWERMAP))
		goto failed;
	strcpy(file, path);
	strcat(file, "/ucdata/properties.dat");
	if (!loadUnicodeTable(&g_ucProps, file, 
			      VERIFY_UNICODE_CHECKSUMS, 
			      CHKSUM_PROPERTIES))
		goto failed;
	strcpy(file, path);
	strcat(file, "/ucdata/combiningclass.dat");
	if (!loadUnicodeTable(&g_ucCombiningClass, file, 
			      VERIFY_UNICODE_CHECKSUMS, 
			      CHKSUM_COMBININGCLASS))
		goto failed;
	strcpy(file, path);
	strcat(file, "/ucdata/scripts.dat");
	if (!loadUnicodeTable(&g_ucScripts, file, 
			      VERIFY_UNICODE_CHECKSUMS, 
			      CHKSUM_SCRIPTS))
		goto failed;
	// MDW: do we need this for converting from X to utf8? or for
	// the is_alnum(), etc. functions?
	if (!loadDecompTables(path) ||
	    !initCompositionTable())
		goto failed;
	//s_convTable.set(1024);
	if ( ! s_convTable.set(4,sizeof(iconv_t),1024,NULL,0,false,0,"cnvtbl"))
		goto failed;
	
	// dont use these files anymore
	if (verifyFiles){
		if (!openIconvDescriptors())
			return log(LOG_WARN,
				   "uni: unable to open all iconv descriptors");
	}		

	return true;
	
failed:
	return log(LOG_WARN, 
		   "uni: unable to load all property tables");
}

char *ucDetectBOM(char *buf, int32_t bufsize){
	if (bufsize < 4) return NULL;
	// copied from ICU
	if(buf[0] == '\xFE' && buf[1] == '\xFF') {
		return  "UTF-16BE";
	} else if(buf[0] == '\xFF' && buf[1] == '\xFE') {
		if(buf[2] == '\x00' && buf[3] =='\x00') {
			return "UTF-32LE";
		} else {
			return  "UTF-16LE";
		}
	} else if(buf[0] == '\xEF' && buf[1] == '\xBB' && buf[2] == '\xBF') {
		return  "UTF-8";
	} else if(buf[0] == '\x00' && buf[1] == '\x00' && 
		  buf[2] == '\xFE' && buf[3]=='\xFF') {
		return  "UTF-32BE";
	}

	return NULL;
}

/*
int32_t 	ucToUnicode(UChar *outbuf, int32_t outbufsize, 
		    char *inbuf, int32_t inbuflen, 
		    const char *charset, int32_t ignoreBadChars,
		    int32_t titleRecVersion){
	g_errno = 0;
	if (inbuflen == 0) return 0;
	// alias for iconv
	const char *csAlias = charset;
       
	if (!strncmp(charset, "x-windows-949", 13)){
		if (titleRecVersion >= 64 && titleRecVersion <= 65)
			csAlias = "WINDOWS-1252";
		else csAlias = "CP949";
			
	}
	if (!strncmp(charset, "Windows-31J", 13)){
		if (titleRecVersion >= 67 || titleRecVersion < 64)
			csAlias = "CP932";
	}

	// Treat all latin1 as windows-1252 extended charset
	if (titleRecVersion < 64){
		if (!strncmp(charset, "ISO-8859-1", 10) )
			csAlias = "WINDOWS-1252";
	}
	else {
		// oops, what about ISO-8859-10?
		if (!strcmp(charset, "ISO-8859-1") )
			csAlias = "WINDOWS-1252";
	}

	iconv_t cd = gbiconv_open("UTF-16LE", csAlias);
	int32_t numBadChars = 0;
	if (cd == (iconv_t)-1) {	
		log("uni: Error opening input conversion"
		    " descriptor for %s: %s (%d)\n", 
		    charset,
		    strerror(errno),errno);
		return 0;		
	}

	//if (normalized) *normalized = false;
	char *pin = (char*)inbuf;
	size_t inRemaining = inbuflen;
	char *pout = (char*)outbuf;
	size_t outRemaining = outbufsize;
	int res = 0;
	if (outbuf == NULL || outbufsize == 0) {
		// just find the size needed for conversion
#define TMP_SIZE 32
		char buf[TMP_SIZE];
		int32_t len = 0;
		while (inRemaining) {
			pout = buf;
			outRemaining = TMP_SIZE;
			res = iconv(cd, &pin, &inRemaining, 
				    &pout, &outRemaining);
			if (res < 0 && errno){
				// convert the next TMP_SIZE block
				if (errno == E2BIG) { 
					len += TMP_SIZE; 
					continue;
				}
				gbiconv_close(cd);
				return 0; // other error
			}
			len += TMP_SIZE-outRemaining;
			len >>= 1; // sizeof UChar
			len += 2; // NULL terminated
			gbiconv_close(cd);
			return len;			
		}
	}

	while (inRemaining && outRemaining) {
		//printf("Before - in: %d, out: %d\n", inRemaining, outRemaining);
	again:
		res = iconv(cd,&pin, &inRemaining,
				&pout, &outRemaining);
		//printf("After - in: %d, out: %d\n", inRemaining, outRemaining);
		//printf("res: %d\n", res);
		if (res < 0 && errno){
			//printf("errno: %s (%d)\n", strerror(errno), errno);
			switch(errno) {
			case EILSEQ:
				numBadChars++;

 				if (ignoreBadChars >= 0 &&
				    numBadChars > ignoreBadChars) {
					g_errno = errno;
					goto done;
				}
				utf16Encode('?', (UChar*)pout);
				pout+=2;outRemaining -= 2;
 				pin++; inRemaining--;
 				continue;
			case EINVAL:
				numBadChars++;



				utf16Encode('?', (UChar*)pout); 
				pout+=2;outRemaining -= 2;
				pin++; inRemaining--;
				continue;
				// go ahead and flag an error now
				// if there is a bad character, we've 
				// probably misguessed the charset

			case E2BIG:
				g_errno = errno;
				//log("uni: error converting to UTF-16: %s",
				//    strerror(errno));
				goto done;
			default:
				//g_errno = errno;
				log("uni: unknown error occurred "
				    "converting to UTF-16: %s (%d)",
				    strerror(errno), errno);
				// clear it and try again
				errno = 0;
				// i saw this happening a lot when rebuilding
				// spiderdb and doing the titledb scan...
				// it was "Resource temporarily unavailable 
				// (11)"
				goto again;
				char *xx=NULL;*xx=0;
				goto done;
			}
		}
	}
done:
	gbiconv_close(cd);
	int32_t len =  (outbufsize - outRemaining) ;
	len = len>=outbufsize-1?outbufsize-2:len;
	len >>= 1;
	//len = outbuf[len]=='\0'?len-1:len;
	outbuf[len] = '\0';
	static char eflag = 1;
	if (numBadChars) {
		if ( eflag )
			log(LOG_DEBUG, "uni: ucToUnicode: got %"INT32" bad chars "
			    "in conversion. Only reported once.", numBadChars);
		// this flag makes it so no bad characters are reported
		// from now on
		//eflag = 0;

		// hmm, we were returning EBADCHARSET, but not aborting 
		// the conversion...this was confusing pageparser -partap
		if (ignoreBadChars > 0 && numBadChars > ignoreBadChars){
			g_errno = EBADCHARSET;
			// needs versioning for old titlerecs which may have
			// aborted after 10 bad chars
			if (titleRecVersion >= 76)
				return 0;
		}
		
	}
	if (res < 0 && g_errno) return 0; 
	return len ;
}
*/


int32_t 	ucToAny(char *outbuf, int32_t outbufsize, char *charset_out,
		 char *inbuf, int32_t inbuflen, char *charset_in,
		 int32_t ignoreBadChars , int32_t niceness ){
	if (inbuflen == 0) return 0;
	// alias for iconv
	char *csAlias = charset_in;
	if (!strncmp(charset_in, "x-windows-949", 13))
		csAlias = "CP949";

	// Treat all latin1 as windows-1252 extended charset
	if (!strncmp(charset_in, "ISO-8859-1", 10) )
		csAlias = "WINDOWS-1252";
	
	iconv_t cd = gbiconv_open(charset_out, csAlias);
	int32_t numBadChars = 0;
	if (cd == (iconv_t)-1) {	
		log("uni: Error opening input conversion"
		    " descriptor for %s: %s (%d)\n", 
		    charset_in,
		    strerror(errno),errno);
		return 0;		
	}

	//if (normalized) *normalized = false;
	char *pin = (char*)inbuf;
	size_t inRemaining = inbuflen;
	char *pout = (char*)outbuf;
	size_t outRemaining = outbufsize;
	int res = 0;
	if (outbuf == NULL || outbufsize == 0) {
		// just find the size needed for conversion
#define TMP_SIZE 32
		char buf[TMP_SIZE];
		int32_t len = 0;
		while (inRemaining) {
			QUICKPOLL(niceness);
			pout = buf;
			outRemaining = TMP_SIZE;
			res = iconv(cd, &pin, &inRemaining, 
				    &pout, &outRemaining);
			if (res < 0 && errno){
				// convert the next TMP_SIZE block
				if (errno == E2BIG) { 
					len += TMP_SIZE; 
					continue;
				}
				gbiconv_close(cd);
				return 0; // other error
			}
			len += TMP_SIZE-outRemaining;
			//len >>= 1; // sizeof UChar
			len += 1; // NULL terminated
			gbiconv_close(cd);
			return len;			
		}
	}

	while (inRemaining && outRemaining) {
		QUICKPOLL(niceness);
		//printf("Before - in: %d, out: %d\n", 
		//inRemaining, outRemaining);
		res = iconv(cd,&pin, &inRemaining,
				&pout, &outRemaining);

		if (res < 0 && errno){
			//printf("errno: %s (%d)\n", strerror(errno), errno);
			g_errno = errno;
			switch(errno) {
			case EILSEQ:
				numBadChars++;

 				if (ignoreBadChars >= 0 &&
				    numBadChars > ignoreBadChars) goto done;
				utf8Encode('?', pout);
				pout++;outRemaining --;
 				pin++; inRemaining--;
				g_errno = 0;
 				continue;
			case EINVAL:
				numBadChars++;

				utf8Encode('?', pout); 
				pout++;outRemaining --;
				pin++; inRemaining--;
				g_errno=0;
				continue;
				// go ahead and flag an error now
				// if there is a bad character, we've 
				// probably misguessed the charset

			case E2BIG:
				//log("uni: error converting to UTF-8: %s",
				//    strerror(errno));
				goto done;
			default:
				log("uni: unknown error occurred "
				    "converting to UTF-8: %s (%d)",
				    strerror(errno), errno);
				goto done;
			}
		}
	}
done:
	gbiconv_close(cd);
	int32_t len =  (outbufsize - outRemaining) ;
	len = len>=outbufsize-1?outbufsize-2:len;
	//len >>= 1;
	//len = outbuf[len]=='\0'?len-1:len;
	outbuf[len] = '\0';
	static char eflag = 1;
	if (numBadChars) {
		if ( eflag )
			log(LOG_DEBUG, "uni: ucToAny: got %"INT32" bad chars "
			    "in conversion 2. Only reported once.",
			    numBadChars);
		// this flag makes it so no bad characters are reported
		// in subsequent conversions
		//eflag = 0;
	}
	if (res < 0 && g_errno) return 0; 
	return len ;
}

// produces a canonical decomposition of UTF-8 input
/*
int32_t utf8CDecompose(	char*       outBuf, int32_t outBufSize,
			const char* inBuf,  int32_t inBufSize,
			bool decodeEntities) {
	const char *p = inBuf;
	const char *pend = inBuf + inBufSize;
	char *q = outBuf;
	char *qend = outBuf + outBufSize;
	while (p < pend) {
		UChar32 c;
		if (decodeEntities)
			c = utf8EntityDecode(p, &p, pend - p);
		else
			c = utf8Decode(p, (char**) &p);
		UChar32 decomp[32];
		int32_t decompLen = recursiveCDExpand(c, decomp, 32);
		for (int i = 0; i < decompLen && (q < qend); i++) {
			UChar32 d = decomp[i];
			unsigned char cc = ucCombiningClass(d);
			// fix out-of-order combining chars
			// Gah...this shouldn't happen too often
			if (cc) {
				char *qq = q; //insert point
				UChar32 c2;
				while (qq  > outBuf){
					char *qprev;
					c2 = utf8Prev(qq, &qprev);
					if (ucCombiningClass(c2) <= cc) break;
					qq = qprev;
				}
				if (qq < q){ // move chars out of the way
					int32_t cSize = utf8Size(c);
					memmove(qq+cSize, qq, (q-qq));
				}
				q += utf8Encode(d, qq);
			}
			else
				q += utf8Encode(d, q);
		}
	}
	return q - outBuf;
}
*/
/*
int32_t ucFromUnicode( char *outbuf, int32_t outbufSize, 
		    const UChar *inbuf, int32_t inbufSize, 
		    const char *charset){
	// alias for iconv
	const char *csAlias = charset;
	if (!strncmp(charset, "x-windows-949", 13) )
		csAlias = "CP949";
	// Treat all latin1 as windows-1252 extended charset
	if (!strncmp(charset, "ISO-8859-1", 10) )
		csAlias = "WINDOWS-1252";
	
	iconv_t cd = gbiconv_open(charset,"UTF-16LE");
	if (cd == (iconv_t)-1) {	
		log("uni: Error opening input conversion"
		    " descriptor for %s: %s (%d)\n", 
		    charset,
		    strerror(errno),errno);
		return 0;		
	}

	char *pin = (char*)inbuf;
	size_t inRemaining = inbufSize<<1;
	char *pout = (char*)outbuf;
	size_t outRemaining = outbufSize;

	if (outbuf == NULL || outbufSize == 0) {
		// just find the size needed for conversion
#define TMP_SIZE 32
		char buf[TMP_SIZE];
		int32_t len = 0;
		while (inRemaining) {
			pout = buf;
			outRemaining = TMP_SIZE;
			int res = iconv(cd, &pin, &inRemaining, 
				    &pout, &outRemaining);
			if (res < 0 && errno){
				// convert the next TMP_SIZE block
				if (errno == E2BIG) { 
					len += TMP_SIZE; 
					continue;
				}
				gbiconv_close(cd);
				// other error
				// shouldn't ever get here
				// maybe we can handle this better...
				// shouldn't take a version change
				// because this function is only used for
				// output
				log(LOG_WARN, "uni: error determining space "
				    "to convert from UTF-16 to %s: %s",
				    charset,
				    strerror(errno));

				return 0; 
			}
			len += TMP_SIZE-outRemaining;
			gbiconv_close(cd);
			return len;			
		}
	}
	while (inRemaining && outRemaining) {
		int res = iconv(cd,&pin, &inRemaining,
				&pout, &outRemaining);

		if (res < 0 && errno){
			switch(errno) {
			case EILSEQ:
			case EINVAL:
				log(LOG_DEBUG, 
				    "uni: Bad character in conversion from "
				    "UTF-16 to %s", charset);
				*pout++ = '?';outRemaining--;
				pin++; inRemaining--;
				continue;
			case E2BIG:
				log("uni: error converting from UTF-16 "
				    "to %s: %s", charset,
				    strerror(errno));
				goto done;
			default:
				log("uni: unknown error occurred "
				    "converting from UTF-16 to %s: %s (%d)",
				    charset,
				    strerror(errno), errno);
				goto done;
			}
		}
	}
done:
	gbiconv_close(cd);
	int32_t len =  outbufSize - outRemaining;
	//len = len>=outbufsize?outbufsize-1:len;
	//len = outbuf[len]=='\0'?len-1:len;
	//outbuf[len] = '\0';
	return len;
}
*/

// Read one UTF-8 character...optionally return the position of the next
// JAB: const-ness for the optimizer...
/*
UChar32 utf8Decode2(const char *p, const char **next){
	int num_bytes = bytes_in_utf8_code[*(unsigned char*)p];
	if (!num_bytes){
		// ill-formed byte sequence
		// lets just return an invalid character and go on to the next
		if (next) *next = p+1;
		return (UChar32)0xffffffff;
	}
	if (next){
		*next = p + num_bytes;
	}
	switch(num_bytes){
	case 1:
		return (UChar32)*p;
	case 2:
		return (UChar32)((*p & 0x1f)<<6 | 
				(*(p+1) & 0x3f));
	case 3:
		return (UChar32)((*p & 0x0f)<<12 | 
				(*(p+1) & 0x3f)<<6 |
				(*(p+2) & 0x3f));
	case 4:
		return (UChar32)((*p & 0x07)<<18 | 
				(*(p+1) & 0x3f)<<12 |
				(*(p+2) & 0x3f)<<6 |
				(*(p+3) & 0x3f));
	default:
		return (UChar32) -1;
	};
}
*/
/*
// starting at 0xc3 0x80  ending at 0xc3 0xbf
static char ascii_c3[] = {
	'A', // 80
	'A', // 81
	'A', // 82
	'A', // 83
	'A', // 84
	'A', // 85
	'A', // 86
	'C', // 87
	'E', // 88
	'E', // 89
	'E', // 8a
	'E', // 8b
	'I', // 8c
	'I', // 8d
	'I', // 8e
	'I', // 8f
	'D', // 90
	'N', // 91
	'O', // 92
	'O', // 93
	'O', // 94
	'O', // 95
	'O', // 96
	'X', // 97 multiplication sign
	'O', // 98
	'U', // 99
	'U', // 9a
	'U', // 9b
	'U', // 9c
	'Y', // 9d
	'P', // 9e thorn
	's', // 9f sharp s
	'a', // a0
	'a', // a1
	'a', // a2
	'a', // a3
	'a', // a4
	'a', // a5
	'a', // a6
	'c', // a7
	'e', // a8
	'e', // a9
	'e', // aa
	'e', // ab
	'i', // ac
	'i', // ad
	'i', // ae
	'i', // af
	'd', // b0
	'n', // b1
	'o', // b2
	'o', // b3
	'o', // b4
	'o', // b5
	'o', // b6
	'X', // b7 division sign
	'o', // b8
	'u', // b9
	'u', // ba
	'u', // bb
	'u', // bc
	'y', // bd
	'p', // be thorn
	'y'  // bf
};

// starting at 0xc4 0x80  ending at 0xc4 0xbf
static char ascii_c4[] = {
	'A', // c4 80
	'a', // c4 81
	'A', // c4 82
	'a', // c4 83
	'A', // c4 84
	'a', // c4 85
	'C', // c4 86
	'c', // c4 87
	'C', // c4 88
	'c', // c4 89
	'C', // c4 8a
	'c',
	'C',
	'c', // c4 8d
	'D', // c4 8e
	'd', // c4 8f
	'D', // c4 90
	'd', // c4 91
	'E', // c4 92
	'e', // 93
	'E', // 94
	'e', // 95
	'E', // 96
	'e', // 97
	'E', // 98
	'e', // 99
	'E', // 9a
	'e', // 9b
	'G', // 9c
	'g', // 9d
	'G', // 9e
	'g', // 9f
	'G', // a0
	'g', // a1
	'G', // a2
	'g', // a3
	'H', // a4
	'h', // a5
	'H', // a6
	'h', // a7
	'I', // a8
	'i', // a9
	'I', // aa
	'i', // ab
	'I', // ac
	'i', // ad
	'I', // ae
	'i', // af
	'I', // b0
	'i', // b1
	'I', // b2 IJ
	'i', // b3 ij
	'J', // b4
	'j', // b5
	'K', // b6
	'k', // b7
	'K', // b8
	'L', // b9
	'l', // ba
	'L', // bb
	'l', // bc
	'L', // bd
	'l', // be
	'L'  // bf
};

// starting at 0xc5 0x80 ending at 0xc5 0xbf
static char ascii_c5[] = {
	'l', // 80
	'L', // 81
	'l', // 82
	'N', // 83
	'n', // 84
	'N', // 85
	'n', // 86
	'N', // 87
	'n', // 88
	'n', // 89
	'N', // 8a
	'n', // 8b
	'O', // 8c
	'o', // 8d
	'O', // 8e
	'o', // 8f
	'O', // 90
	'o', // 91
	'O', // 92 OE
	'o', // 93 oe
	'R', // 94
	'r', // 95
	'R', // 96
	'r', // 97
	'R', // 98
	'r', // 99
	'S', // 9a
	's', // 9b
	'S', // 9c
	's', // 9d
	'S', // 9e
	's', // 9f
	'S', // a0
	's', // a1
	'T', // a2
	't', // a3
	'T', // a4
	't', // a5
	'T', // a6
	't', // a7
	'U', // a8
	'u', // a9
	'U', // aa
	'u', // ab
	'U', // ac
	'u', // ad
	'U', // ae
	'u', // af
	'U', // b0
	'u', // b1
	'U', // b2
	'u', // b3
	'W', // b4
	'w', // b5
	'Y', // b6
	'y', // b7
	'Y', // b8
	'Z', // b9
	'z', // ba
	'Z', // bb
	'z', // bc
	'Z', // bd
	'z', // be
	's'  // bf (int32_t s)
};


// starting at 0xc6 0x80 ending at 0xc6 0xbf
static char ascii_c6[] = {
	'b', // 80
	'B', // 81
	'B', // 82
	'b', // 83
	'B', // 84
	'b', // 85
	'C', // 86
	'C', // 87
	'c', // 88
	'D', // 89
	'D', // 8a
	'D', // 8b
	'd', // 8c
	'd', // 8d
	'E', // 8e
	'E', // 8f
	'E', // 90
	'F', // 91
	'f', // 92
	'G', // 93
	'G', // 94
	'h', // 95 hv
	'I', // 96
	'I', // 97
	'K', // 98
	'k', // 99
	'l', // 9a
	'l', // 9b
	'M', // 9c
	'N', // 9d
	'n', // ie
	'O', // 9f
	'O', // a0
	'o', // a1
	'O', // a2 OI
	'o', // a3 oi
	'P', // a4
	'p', // a5
	'R', // a6 YR
	'S', // a7
	's', // a8
	'S', // a9
	'S', // aa
	't', // ab
	'T', // ac
	't', // ad
	'T', // ae
	'U', // af
	'u', // b0
	'U', // b1
	'V', // b2
	'Y', // b3
	'y', // b4
	'Z', // b5
	'z', // b6
	'z', // b7
	'z', // b8
	'z', // b9
	'z', // ba
	'z', // bb
	'z', // bc
	'z', // bd
	'z', // be
	'p'  // bf
};

class bichar {
public:
	char m_c0;
	char m_c1;
};

// maps utf8 chars starting with 0xce into utf8 char w/o the accent mark
static bichar utf_ce[] = {
	{0xce,0x00}, // 80
	{0xce,0x00}, // 81
	{0xce,0x00}, // 82
	{0xce,0x00}, // 83
	{0xce,0x00}, // 84
	{0xce,0x00}, // 85
	{0xce,0x91}, // 86 Ά to Α
	{0xce,0x00}, // 87
	{0xce,0x95}, // 88 Έ to Ε
	{0xce,0x00}, // 89 Ή to Η
	{0xce,0x99}, // 8a Ί to I
	{0xce,0x00}, // 8b
	{0xce,0x9f}, // 8c Ό to O
	{0xce,0x00}, // 8d
	{0xce,0xa5}, // 8e Ύ to Y
	{0xce,0xa9}, // 8f Ώ to Ω
	{0xce,0xb9}, // 90 ΐ to ι
	{0xce,0x91}, // 91 A to A
	{0xce,0x92}, // 92 B to B
	{0xce,0x93}, // 93 Γ to Γ
	{0xce,0x94}, // 94 Δ to Δ
	{0xce,0x95}, // 95 hv
	{0xce,0x96}, // 96
	{0xce,0x97}, // 97
	{0xce,0x98}, // 98
	{0xce,0x99}, // 99
	{0xce,0x9a}, // 9a
	{0xce,0x9b}, // 9b
	{0xce,0x9c}, // 9c
	{0xce,0x9d}, // 9d
	{0xce,0x9e}, // 9e
	{0xce,0x9f}, // 9f
	{0xce,0xa0}, // a0
	{0xce,0xa1}, // a1
	{0xce,0x00}, // a2
	{0xce,0xa3}, // a3
	{0xce,0xa4}, // a4
	{0xce,0xa5}, // a5
	{0xce,0xa6}, // a6 YR
	{0xce,0xa7}, // a7
	{0xce,0xa8}, // a8
	{0xce,0xa9}, // a9
	{0xce,0x99}, // aa Ϊ to I
	{0xce,0xa5}, // ab Ϋ to Y
	{0xce,0xb1}, // ac ά to α
	{0xce,0xb5}, // ad έ to ε
	{0xce,0xb7}, // ae ή
	{0xce,0xb9}, // af ί
	{0xcf,0x85}, // b0 ΰ
	{0xce,0xb1}, // b1
	{0xce,0xb2}, // b2
	{0xce,0xb3}, // b3
	{0xce,0xb4}, // b4
	{0xce,0xb5}, // b5
	{0xce,0xb6}, // b6
	{0xce,0xb7}, // b7
	{0xce,0xb8}, // b8
	{0xce,0xb9}, // b9
	{0xce,0xba}, // ba
	{0xce,0xbb}, // bb
	{0xce,0xbc}, // bc
	{0xce,0xbd}, // bd
	{0xce,0xbe}, // be
	{0xce,0xbf}  // bf
};

// maps utf8 chars starting with 0xcf into utf8 char w/o the accent mark
static bichar utf_cf[] = {
	{0xcf,0x80}, // 80
	{0xcf,0x81}, // 81
	{0xcf,0x82}, // 82
	{0xcf,0x83}, // 83
	{0xcf,0x84}, // 84
	{0xcf,0x85}, // 85
	{0xcf,0x86}, // 86
	{0xcf,0x87}, // 87
	{0xcf,0x88}, // 88
	{0xcf,0x89}, // 89
	{0xce,0xb9}, // 8a ϊ to ι
	{0xcf,0x85}, // 8b ϋ
	{0xce,0xbf}, // 8c ό
	{0xcf,0x85}, // 8d ύ
	{0xcf,0x89}, // 8e ώ
	{0xcf,0x8f}, // 8f
	{0xcf,0x90}, // 90
	{0xcf,0x91}, // 91
	{0xce,0x85}, // 92 ϒ to Y
	{0xce,0x85}, // 93 ϓ to Y
	{0xce,0x85}, // 94 ϔ to Y
	{0xcf,0x95}, // 95
	{0xcf,0x89}, // 96 ϖ to ω (is this right? - mdw)
	{0xcf,0x97}, // 97
	{0xcf,0x98}, // 98
	{0xcf,0x99}, // 99
	{0xcf,0x9a}, // 9a
	{0xcf,0x9b}, // 9b
	{0xcf,0x9c}, // 9c
	{0xcf,0x9d}, // 9d
	{0xcf,0x9e}, // 9e
	{0xcf,0x9f}, // 9f
	{0xcf,0xa0}, // a0
	{0xcf,0xa1}, // a1
	{0xcf,0xa2}, // a2
	{0xcf,0xa3}, // a3
	{0xcf,0xa4}, // a4
	{0xcf,0xa5}, // a5
	{0xcf,0xa6}, // a6
	{0xcf,0xa7}, // a7
	{0xcf,0xa8}, // a8
	{0xcf,0xa9}, // a9
	{0xcf,0xaa}, // aa
	{0xcf,0xab}, // ab
	{0xcf,0xac}, // ac
	{0xcf,0xad}, // ad
	{0xcf,0xae}, // ae
	{0xcf,0xaf}, // af
	{0xcf,0xb0}, // b0
	{0xcf,0xb1}, // b1
	{0xcf,0xb2}, // b2
	{0xcf,0xb3}, // b3
	{0xcf,0xb4}, // b4
	{0xcf,0xb5}, // b5
	{0xcf,0xb6}, // b6
	{0xcf,0xb7}, // b7
	{0xcf,0xb8}, // b8
	{0xcf,0xb9}, // b9
	{0xcf,0xba}, // ba
	{0xcf,0xbb}, // bb
	{0xcf,0xbc}, // bc
	{0xcf,0xbd}, // bd
	{0xcf,0xb9}, // be Ͼ
	{0xcf,0xbd}  // bf Ͽ
};
*/	

/*
//int32_t utf8ToAscii(char *outbuf, int32_t outbufsize,
int32_t stripAccentMarks (char *outbuf, int32_t outbufsize,
		       unsigned char *p, int32_t inbuflen) { // inbuf

	char *dst = outbuf;
	unsigned char *pend = p + inbuflen;
	char *dend = outbuf + outbufsize;
	char cs;
	for ( ; p < pend ; p += cs ) {
		// do not breach
		if ( dst >= dend ) break;
		// get the size
		cs = getUtf8CharSize(p);
		// deal with one ascii char quickly
		if ( cs == 1 ) {
			*dst++ = *p;
			continue;
		}
		// we do not know how to convert this!
		if ( cs != 2 ) return -1;
		// standard crap
		char *table ;
		if      ( *p == 0xc3 ) table = ascii_c3;
		else if ( *p == 0xc4 ) table = ascii_c4;
		else if ( *p == 0xc5 ) table = ascii_c5;
		else if ( *p == 0xc6 ) table = ascii_c6;
		else if ( *p == 0xce ) {
			bichar *btab  = utf_ce;
			if ( p[1] < 0x80 ) return -1;
			*dst++ = btab[p[1]-0x80].m_c0;
			*dst++ = btab[p[1]-0x80].m_c1;
			continue;
		}
		else if ( *p == 0xcf ) {
			bichar *btab = utf_cf;
			if ( p[1] < 0x80 ) return -1;
			*dst++ = btab[p[1]-0x80].m_c0;
			*dst++ = btab[p[1]-0x80].m_c1;
			continue;
		}
		else return -1;

		if ( p[1] < 0x80 ) return -1;
		if ( p[1] > 0xbf ) return -1;

		*dst++ = table[p[1]-0x80];
	}
	// null term just in case
	*dst = '\0';
	return dst - outbuf;
}
*/


int32_t stripAccentMarks (char *outbuf, int32_t outbufsize,
		       unsigned char *p, int32_t inbuflen) {
	char *s = (char *)p;
	char *send = (char *)p + inbuflen;
	int32_t cs;
	char *dst = outbuf;
	for ( ; s < send ; s += cs ) {
		// how big is this character?
		cs = getUtf8CharSize(s);
		// convert the utf8 character to UChar32
		UChar32 uc = utf8Decode ( s );
		// break "uc" into decomposition of UChar32s
		UChar32 ttt[32];
		int32_t klen = recursiveKDExpand(uc,ttt,32);
		if(klen>32){char *xx=NULL;*xx=0;}
		// sanity
		if ( dst + 5 > outbuf+outbufsize ) return -1;
		// if the same, leave it! it had no accent marks or other
		// modifiers...
		if ( klen <= 1 ) {
			gbmemcpy ( dst , s , cs );
			dst += cs;
			continue;
		}
		// take the first one as the stripped
		// convert back to utf8
		int32_t stored = utf8Encode ( ttt[0] , dst );
		// skip over the stored utf8 char
		dst += stored;
	}
	// sanity. breach check
	if ( dst > outbuf+outbufsize ) { char *xx=NULL;*xx=0; }
	// return # of bytes stored into outbuf
	return dst - outbuf;
}


// helper function for printing unicode text range
// slen is length in UChars
/*
int32_t ucToAscii(char *buf, int32_t bufsize, UChar *s, int32_t slen){
	int32_t count=0;
	for (UChar *p = s ; 
	     p < (s+slen) && count < bufsize-1 ; ) {
		UChar32 c = utf16Decode(p, &p);
		// ASCII
		if (c < 0x80 && c >= 0x20) { buf[count++] = (char)c;continue;}
		// Unicode BMP
		if (c < 0x10000){
			// not enough room to encode with NULL
			if (bufsize - count <= 8) 
				break;
			if (c<0x20)
				sprintf(buf+count,"[U+%02lX]", c); 
			else
				sprintf(buf+count,"[U+%04lX]", c); 
			count += gbstrlen(buf+count);
			continue;
		}
		// Big(!) Unicode
		// not enough room to encode with NULL
		if (bufsize - count <= 10) 
			break;
		sprintf(buf+count,"[U+%04lX]", c); 
		count += gbstrlen(buf+count);
		continue;
	}
	buf[count++]='\0';

	return count;

}

// char* version
int32_t ucToAscii(char *buf, int32_t bufsize, char *s, int32_t slen){
	return ucToAscii(buf, bufsize, (UChar*)s, slen/2);
}
*/

//static char s_dbuf[4096];

//char *uccDebug(char *s, int32_t slen){
//	ucToAscii(s_dbuf, 4096, s, slen);
//	return s_dbuf;
//}


//char *ucUDebug(UChar *s, int32_t slen){
//	ucToAscii(s_dbuf, 4096, s, slen);
//	return s_dbuf;
//}

static iconv_t cd_latin1_u8 = (iconv_t)-1;
int32_t latin1ToUtf8(char *outbuf, int32_t outbufsize, 
		  char *inbuf, int32_t inbuflen){
	// fixed from < 0 to == (iconv_t)-1 thanks to isj 
	// (gcc optimized the condition out)
	if ( cd_latin1_u8 == (iconv_t)-1 ) {
		cd_latin1_u8 = gbiconv_open("UTF-8", "WINDOWS-1252");
		// fixed from < 0 to == (iconv_t)-1 thanks to isj
		// (gcc optimized the condition out)
		if ( cd_latin1_u8 == (iconv_t)-1 ) {	
			log("uni: Error opening output conversion"
			    " descriptor for utf-8: %s (%d)\n", 
			    strerror(g_errno),g_errno);
			return 0;		
		}
	}

	char *pin = (char*)inbuf;
	size_t inRemaining = inbuflen;
	char *pout = outbuf;
	size_t outRemaining = outbufsize;
	while (inRemaining && outRemaining) {
		int res = iconv(cd_latin1_u8,&pin, &inRemaining,
				&pout, &outRemaining);
		if (res < 0 && errno){
			switch(errno) {
			case EILSEQ:
			case EINVAL:
				log(LOG_DEBUG, 
				    "uni: Bad character in utf-8 conversion");
				*pout++ = '?';outRemaining--;
				pin++; inRemaining--;
				continue;
			case E2BIG:
				// this happens a bunch when we are guessing
				// the charset i think, so don't spam the
				// log with warning, keep it a LOG_INFO
				// I'm making this a log debug --zak
				log(LOG_DEBUG,
				    "uni: error converting to utf-8: %s",
				    strerror(errno));
				goto done;
			default:
				log("uni: unknown error occurred "
				    "converting to utf-8: %s (%d)",
				    strerror(errno), errno);
				goto done;
			}
		}
	}
done:
	int32_t len =  outbufsize - outRemaining;
	len = len>=outbufsize?outbufsize-1:len;
	//len = outbuf[len]=='\0'?len-1:len;
	outbuf[len] = '\0';
	return len;
	
}
/*

static iconv_t cd_u16_u8 = (iconv_t)-1;
int32_t utf16ToUtf8(char *outbuf, int32_t outbufsize, 
		   UChar *inbuf, int32_t inbuflen){
	if ((int)cd_u16_u8 < 0) {
		//printf("opening iconv descriptor\n");
		cd_u16_u8 = gbiconv_open("UTF-8", "UTF-16LE");
		if ((int)cd_u16_u8 < 0) {	
			log("uni: Error opening output conversion"
			    " descriptor for utf-8: %s (%d)\n", 
			    strerror(errno),errno);
			return 0;		
		}
	}

	char *pin = (char*)inbuf;
	size_t inRemaining = inbuflen << 1;
	char *pout = outbuf;
	size_t outRemaining = outbufsize;
	if (!inbuf) return 0;
	while (inRemaining && outRemaining) {
		int res = iconv(cd_u16_u8,&pin, &inRemaining,
				&pout, &outRemaining);

		if (res < 0 && errno){

			switch(errno) {
			case EILSEQ:
			case EINVAL:
				log(LOG_DEBUG, 
				    "uni: Bad character in utf-8 conversion");
				*pout++ = '?';outRemaining--;
				pin++; inRemaining--;
				continue;
			case E2BIG:
				// this happens a bunch when we are guessing
				// the charset i think, so don't spam the
				// log with warning, keep it a LOG_INFO
				log(LOG_DEBUG,
				    "uni: error converting to utf-8: %s",
				    strerror(errno));
				goto done;
			default:
				log("uni: unknown error occurred "
				    "converting to utf-8: %s (%d)",
				    strerror(errno), errno);
				goto done;
			}
		}
	}
done:
	int32_t len =  outbufsize - outRemaining;
	len = len>=outbufsize?outbufsize-1:len;
	outbuf[len] = '\0';
	return len;
	
}

static iconv_t cd_u16_latin1 = (iconv_t)-1;
int32_t utf16ToLatin1(char *outbuf, int32_t outbufsize, 
		   UChar *inbuf, int32_t inbuflen){
	if ((int)cd_u16_latin1 < 0) {
		//printf("opening iconv descriptor\n");
		cd_u16_latin1 = gbiconv_open("WINDOWS-1252", "UTF-16LE");
		if ((int)cd_u16_latin1 < 0) {	
			log("uni: Error opening output conversion"
			    " descriptor for latin1: %s (%d)\n", 
			    strerror(errno),errno);
			return 0;		
		}
	}

	char *pin = (char*)inbuf;
	size_t inRemaining = inbuflen << 1;
	char *pout = outbuf;
	size_t outRemaining = outbufsize;
	static char eflag = 1;
	if (!inbuf) return 0;
	while (inRemaining && outRemaining) {
		int res = iconv(cd_u16_latin1,&pin, &inRemaining,
				&pout, &outRemaining);
		if (res < 0 && errno){
			switch(errno) {
			case EILSEQ:
			case EINVAL:
				if ( eflag )
					log(LOG_DEBUG, 
					    "uni: Bad character in latin1 "
					    "conversion. Only reported once.");
				eflag = 0;
				*pout++ = '?';outRemaining--;
				pin++; inRemaining--;
				continue;
			case E2BIG:
				log("uni: error converting to latin1: %s",
				    strerror(errno));
				goto done;
			default:
				log("uni: unknown error occurred "
				    "converting to latin1: %s (%d)",
				    strerror(errno), errno);
				goto done;
			}
		}
	}
done:
	int32_t len =  outbufsize - outRemaining;
	len = len>=outbufsize?outbufsize-1:len;

	outbuf[len] = '\0';
	return len;
	
}

int32_t utf16ToUtf8_intern(char* outbuf, int32_t outbufSize, 
		 UChar *s, int32_t slen){
	UChar *p = s;
	UChar *next = NULL;
	UChar32 c;
	char *q = outbuf;
	
	while(p && p < (s+slen)) {
		c = utf16Decode(p, &next);
		p = next;
		if ((q+4)< (outbuf+outbufSize))
			q += utf8Encode(c,q);
		else break;
	}
	return q - outbuf;	
}

// . convert a UTF-16 str to UTF-8
// . if buf is NULL, allocate memory for the conversion
// . return NULL on error
char *utf16ToUtf8Alloc( char *utf16Str, int32_t utf16StrLen,
			char *buf, int32_t *bufSize ) {
	int32_t size = 0;
	if ( ! buf ) {
		size = ucFromUnicode( NULL, 0,
				      (UChar *)utf16Str, utf16StrLen>>1,
				      "UTF-8" );

		buf = (char *)mmalloc( size, "utf8str" );
		if ( ! buf ) {
			g_errno = ENOMEM;
			log( "query: Could not allocate %"INT32" bytes for "
			     "utf16toUtf8Alloc", size );
			return NULL;
		}
	}
	
	errno = 0;
	int32_t resLen = ucFromUnicode( buf, *bufSize, 
				     (UChar *)utf16Str, utf16StrLen>>1,
				     "UTF-8" );

	if ( errno ) {
		if ( size != 0 ) {
			mfree( buf, size, "utf8str" );
			buf = NULL;
		}
		*bufSize = 0;
		return NULL;
	}

	if ( size != 0 ) *bufSize = size;
	else             *bufSize = resLen;

	return buf;
}
*/
/*
#if 0
// For testing purposes
int utf8_parse_buf(char *s){
	char *p = s;
	while (p && *p){
		UChar32 c = utf8Decode(p, &p);
		if (c == (UChar32)-1){
			fprintf(stderr, "Error: invalid character at pos %d\n",
				(p - s));
			return -1;
		}
		ucPutc(c);
	}
	
	return 0;
}
#endif
*/

/*
int32_t ucAtoL(UChar* buf, int32_t len) {
	int32_t ret = 0;
	bool inNumber=false;
	int32_t sign = 1;  // plus or minus 1
	for (UChar *p = buf;
	     p < (buf+len) ; ){
		UChar32 c = utf16Decode(p, &p);
		if (!inNumber && c == '-') {
			sign = -1; 
			continue;
		}
		inNumber = true;
		if (!ucIsDigit(c)) return ret;
		ret *= 10;
		ret += ucDigitValue(c);		
	}
	return ret;
}

int32_t ucTrimWhitespaceInplace(UChar * buf, int32_t bufLen) {

	UChar *start = buf;
	int32_t newLen = bufLen;
	UChar *p = buf;
	while(p < buf+bufLen){
		UChar *pnext;
		UChar32 c = utf16Decode(p, &pnext);
		if (ucIsWordChar(c)) break;
		start = p;
		//newLen -= pnext-p;
		p = pnext;
	}
	start = p;
	newLen -= (p - buf);
	p = buf+bufLen;
	while(p > start) {
		UChar *pp;
		UChar32 c = utf16Prev(p, &pp);
		if (ucIsWordChar(c)) break;
		p = pp;
	}
	newLen -= (buf+bufLen) - p;
	if (buf != start)
		memmove(buf, start, newLen<<1);
	return newLen;
}

// FIXME: Whacketty-hacketty
// This is only used in one spot (nofollow)so I'm ignoring all the 
// Unicode collation and normalization stuff right now
int32_t ucStrCaseCmp(UChar *s1, int32_t slen1, UChar*s2, int32_t slen2) {
	int32_t len = slen1;
	if (slen2 < len) len = slen2;
	UChar *p = s1;
	UChar *q = s2;

	while ( p - s1 < len ) {
		UChar32 c1 = ucToLower(utf16Decode(p, &p));
		UChar32 c2 = ucToLower(utf16Decode(q, &q));
		if (c1 < c2) return -1;
		if (c1 > c2) return 1;
	}
	// strings are identical...unless one is int16_ter
	if (slen1 < slen2) return -1;
	if (slen1 > slen2) return 1;
	
	return 0;
}
int32_t ucStrCaseCmp(UChar *s1, int32_t slen1, char*s2, int32_t slen2) {
	int32_t len = slen1;
	if (slen2 < len) len = slen2;
	UChar *p = s1;
	char *q = s2;

	while ( p - s1 < len ) {
		UChar32 c1 = ucToLower(utf16Decode(p, &p));
		UChar32 c2 = to_lower(*q++);
		if (c1 < c2) return -1;
		if (c1 > c2) return 1;
	}
	// strings are identical...unless one is int16_ter
	if (slen1 < slen2) return -1;
	if (slen1 > slen2) return 1;
	
	return 0;
}

int32_t ucStrCmp(UChar *s1, int32_t slen1, UChar*s2, int32_t slen2) {
	int32_t len = slen1;
	if (slen2 < len) len = slen2;
	UChar *p = s1;
	UChar *q = s2;

	while ( p - s1 < len ) {
		UChar32 c1 = utf16Decode(p, &p);
		UChar32 c2 = utf16Decode(q, &q);
		if (c1 < c2) return -1;
		if (c1 > c2) return 1;
	}
	// strings are identical...unless one is int16_ter
	if (slen1 < slen2) return -1;
	if (slen1 > slen2) return 1;
	
	return 0;
}

int32_t ucStrNLen(UChar *s, int32_t maxLen) {
	int32_t len = 0;
	while (len < maxLen && s[len]) len++;
	return len;
}
// look for an ascii substring in a utf-16 string
UChar *ucStrNCaseStr(UChar *haystack, int32_t haylen, char *needle) {
	int32_t matchLen = 0;
	int32_t needleLen = gbstrlen(needle);
	for (int32_t i = 0; i < haylen;i++){
		UChar32 c1 = ucToLower(haystack[i]);
		UChar32 c2 = to_lower(needle[matchLen]);
		if ( c1 != c2 ){
			// no match
			matchLen = 0;
			continue;
		}
		// we matched another character
		matchLen++;
		if (matchLen < needleLen) continue;
		
		// we've matched the whole string
		return haystack + i - matchLen + 1;
	}
	return NULL;
}

UChar *ucStrNCaseStr(UChar *haystack, int32_t haylen, char *needle, 
		     int32_t needleLen) {
	int32_t matchLen = 0;
	for (int32_t i = 0; i < haylen;i++){
		UChar32 c1 = ucToLower(haystack[i]);
		UChar32 c2 = to_lower(needle[matchLen]);
		if ( c1 != c2 ){
			// no match
			matchLen = 0;
			continue;
		}
		// we matched another character
		matchLen++;
		if (matchLen < needleLen) continue;
		
		// we've matched the whole string
		return haystack + i - matchLen + 1;
	}
	return NULL;
}

// look for a utf-16 substring in a utf-16 string
UChar *ucStrNCaseStr(UChar *haystack, int32_t haylen,
		     UChar *needle, int32_t needleLen) {
	int32_t matchLen = 0;
	for (int32_t i = 0; i < haylen;i++){
		UChar32 c1 = ucToLower(haystack[i]);
		UChar32 c2 = ucToLower(needle[matchLen]);
		if ( c1 != c2 ){
			// no match
			matchLen = 0;
			continue;
		}
		// we matched another character
		matchLen++;
		if (matchLen < needleLen) continue;
		
		// we've matched the whole string
		return haystack + i - matchLen + 1;
	}
	return NULL;
}

// look for a unicode substring in an ascii string
char *ucStrNCaseStr(char *haystack,
		    UChar *needle, int32_t needleLen) {
	int32_t matchLen = 0;
	for (char *h = haystack; *h; h++) {
		UChar32 c1 = to_lower(*h);
		UChar32 c2 = ucToLower(needle[matchLen]);
		if ( c1 != c2 ) {
			// no match
			matchLen = 0;
			continue;
		}
		// we matched another character
		matchLen++;
		if (matchLen < needleLen) continue;

		// we've matched the whole string
		return h - matchLen + 1;
	}
	return NULL;
}

// look for a unicode substring in an ascii string
char *ucStrNCaseStr(char *haystack, int32_t haylen,
		    UChar *needle, int32_t needleLen) {
	int32_t matchLen = 0;
	for (char *h = haystack; h-haystack < haylen; h++) {
		UChar32 c1 = to_lower(*h);
		UChar32 c2 = ucToLower(needle[matchLen]);
		if ( c1 != c2 ) {
			// no match
			matchLen = 0;
			continue;
		}
		// we matched another character
		matchLen++;
		if (matchLen < needleLen) continue;

		// we've matched the whole string
		return h - matchLen + 1;
	}
	return NULL;
}
*/

void resetUnicode ( ) {
	//s_convTable.reset();
	gbiconv_reset();
}

bool openIconvDescriptors() {

	// why do this when we call gbiconv_open() directly from ucToAny()
	// and other functions?
	return true;

	for (int i=2; i <= 2258 ; i++ ){
		if (!supportedCharset(i)) continue;

		char *charset = get_charset_str(i);
		if (!charset) return false;
		
		char *csAlias = charset;
		if (!strncmp(charset, "x-windows-949", 13))
			csAlias = "CP949";
		
		// Treat all latin1 as windows-1252 extended charset
		if (!strncmp(charset, "ISO-8859-1", 10) )
			csAlias = "WINDOWS-1252";
		if (!strncmp(charset, "Windows-31J", 13)){
			csAlias = "CP932";
		}
		
		//iconv_t cd1 = gbiconv_open("UTF-16LE", csAlias);
		iconv_t cd1 = gbiconv_open("UTF-8", csAlias);
		if (cd1 == (iconv_t)-1) {	
		 	//return false;
		}

		//iconv_t cd2 = gbiconv_open(csAlias, "UTF-16LE");
		iconv_t cd2 = gbiconv_open(csAlias, "UTF-8");
		if (cd2 == (iconv_t)-1) {	
			//return false;
		}
	}
	// ...and the ones that don't involve utf16
	if (gbiconv_open("UTF-8", "WINDOWS-1252") < 0) return false;
	if (gbiconv_open("WINDOWS-1252", "UTF-8") < 0) return false;
	
	//log(LOG_INIT, "uni: Successfully loaded all iconv descriptors");
	return true;
}
