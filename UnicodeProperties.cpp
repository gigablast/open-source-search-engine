#include "gb-include.h"

#include "Mem.h"
#include "Unicode.h"

UCPropTable g_ucLowerMap(sizeof(UChar32), 9);
UCPropTable g_ucUpperMap(sizeof(UChar32), 9);
//UCPropTable g_ucCategory(sizeof(u_short), 8);
UCPropTable g_ucProps(sizeof(UCProps), 8);
UCPropTable g_ucScripts(sizeof(UCScript), 10);
UCPropTable g_ucKDIndex(sizeof(long), 8);
// JAB: we now have Kompatible and Canonical decomposition
UCPropTable g_ucCDIndex(sizeof(long), 8);
UCPropTable g_ucCombiningClass(sizeof(u_char), 9);

// Kompatible Decomposition
static char 	  *s_ucKDData = NULL;
static u_long      s_ucKDDataSize = 0;
static u_long      s_ucKDAllocSize = 0;

// JAB: Canonical Decomposition
static char 	  *s_ucCDData = NULL;
static u_long      s_ucCDDataSize = 0;
static u_long      s_ucCDAllocSize = 0;

unsigned long calculateChecksum(char *buf, long bufLen);
char *g_ucScriptNames[] = {
	"Common",
	"Arabic",
	"Armenian",
	"Bengali",
	"Bopomofo",
	"Braille",
	"Buhid",
	"Canadian_Aboriginal",
	"Cherokee",
	"Cypriot",
	"Cyrillic",
	"Deseret",
	"Devanagari",
	"Ethiopic",
	"Georgian",
	"Gothic",
	"Greek",
	"Gujarati",
	"Gurmukhi",
	"Han",
	"Hangul",
	"Hanunoo",
	"Hebrew",
	"Hiragana",
	"Inherited",
	"Kannada",
	"Katakana",
	"Katakana_Or_Hiragana",
	"Khmer",
	"Lao",
	"Latin",
	"Limbu",
	"Linear_B",
	"Malayalam",
	"Mongolian",
	"Myanmar",
	"Ogham",
	"Old_Italic",
	"Oriya",
	"Osmanya",
	"Runic",
	"Shavian",
	"Sinhala",
	"Syriac",
	"Tagalog",
	"Tagbanwa",
	"Tai_Le",
	"Tamil",
	"Telugu",
	"Thaana",
	"Thai",
	"Tibetan",
	"Ugaritic",
	"Yi"
};

bool saveUnicodeTable(UCPropTable *table, char *filename) {
	size_t tableSize = table->getStoredSize();
	char *buf = (char*)mmalloc(tableSize,"UP1");
	if (!buf){
		log(LOG_WARN, "uni: Couldn't allocate %d bytes "
		       "for storing %s", tableSize,filename);
		return false;
	}
	
	if (!table->serialize(buf, tableSize)) {
		mfree(buf,tableSize,"UP1");
		log(LOG_WARN, "uni: Error serializing %s", 
		    filename);
		return false;
	}
	
	FILE *fp = fopen(filename, "w");
	if (!fp) {
		mfree(buf,tableSize,"UP1");
		log(LOG_WARN, "uni: "
		    "Couldn't open %s for writing: %s", 
		       filename, strerror(errno));
		return false;
	}
	
	size_t nwrite = fwrite(buf, tableSize, 1, fp);
	if (nwrite != 1) {
		log(LOG_WARN, "uni: Error writing %s", 
		    filename);
		mfree(buf,tableSize,"UP1");
		fclose(fp);
		return false;
	}
	mfree(buf,tableSize,"UP1");
	fclose(fp);
	return true;


}


bool loadUnicodeTable(UCPropTable *table, char *filename, bool useChecksum, unsigned long expectedChecksum) {

	FILE *fp = fopen(filename, "r");
	if (!fp) 
		return log(LOG_WARN,
			   "uni: Couldn't open %s "
			   "for reading", filename);
	fseek(fp,0,SEEK_END);
	size_t fileSize = ftell(fp);
	rewind(fp);
	char *buf = (char*)mmalloc(fileSize, "Unicode");
	if (!buf) {
		fclose(fp);
		return log(LOG_WARN, 
			   "uni: No memory to load %s", filename);
	}
	size_t nread = fread(buf, 1, fileSize, fp);
	if (nread != fileSize) {
		fclose(fp);
		mfree(buf, fileSize, "Unicode");
		return log(LOG_WARN, 
			   "uni: error reading %s", filename);
	}

	unsigned long chksum = calculateChecksum(buf, fileSize);
	//log(LOG_INFO, "uni: checksum for %s: %ld",
	//    filename, chksum);
	if (useChecksum && (expectedChecksum != chksum)) {
		fclose(fp);
		mfree(buf, fileSize, "Unicode");
		return log(LOG_WARN, "uni: checksum failed for %s", 
		    filename);
	}

	if (!table->deserialize(buf, fileSize)) {
		fclose(fp);
		mfree(buf, fileSize, "Unicode");
		return log(LOG_WARN,
			   "uni: error deserializing %s", filename);
	}
	fclose(fp);
	mfree(buf, fileSize, "Unicode");
	return true;
}


bool setKDValue(UChar32 c, UChar32* decomp, long decompCount, bool fullComp) {
        unsigned long size = sizeof(decompCount) + 
		decompCount*sizeof(UChar32);
		
	if (s_ucKDDataSize+size > s_ucKDAllocSize){
		if (!s_ucKDData) {
			s_ucKDData = (char*)mmalloc(4096, 
						    "UnicodeProperties");
			if (!s_ucKDData)
				return log(LOG_WARN, "uni: "
					   "Out of Memory");
			s_ucKDAllocSize = 4096;			
			//dummy value for 0 index
			*(long*)s_ucKDData = 0xffffffff;
			s_ucKDDataSize = sizeof(long);
		}
		else {
			unsigned long newSize = s_ucKDAllocSize + 4096;
			char *newBuf = (char*)mrealloc(s_ucKDData, 
						       s_ucKDAllocSize,
						       newSize, 
						       "UnicodeProperties");
			if (!newBuf)
				return log(LOG_WARN, "uni: "
					   "Out of Memory");
			s_ucKDAllocSize = newSize;
			s_ucKDData = newBuf;
		}
		
	}
	// store fullComp flag in high bit of decompCount
	if (fullComp) 
		*(long*)(s_ucKDData+s_ucKDDataSize) = decompCount | 0x80000000;
	else
		*(long*)(s_ucKDData+s_ucKDDataSize) = decompCount;

	memcpy(s_ucKDData+s_ucKDDataSize+sizeof(decompCount), decomp, 
	       decompCount*sizeof(UChar32));
	long pos = s_ucKDDataSize;
	s_ucKDDataSize += size;
	
	return g_ucKDIndex.setValue(c, (void*)&pos);
}

UChar32 *getKDValue(UChar32 c, long *decompCount, bool *fullComp) {
	*decompCount = 0;
	if (fullComp) *fullComp = false;
	long *pos = (long*)g_ucKDIndex.getValue(c);
	if (!pos || !*pos) return NULL;
	*decompCount = (*(long*)(&s_ucKDData[*pos])) & 0x7fffffff;
	if (fullComp) *fullComp = (*(long*)(&s_ucKDData[*pos])) & 0x80000000;
	return (UChar32*) (&s_ucKDData[*pos+sizeof(long)]);
}

long recursiveKDExpand(UChar32 c, UChar32 *buf, long bufSize) {
	long decompCount = 0;
	UChar32 *decomp = getKDValue(c, &decompCount);
	if (!decompCount) {
		buf[0] = c;
		return 1;
	}

	long decompIndex = 0;
	for (int i=0;i<decompCount;i++) {
		decompIndex += recursiveKDExpand(decomp[i], 
						 buf+decompIndex,
						 bufSize-decompIndex);
	}
	return decompIndex;
}

// JAB: lazy engineer cut-n-paste job
bool setCDValue(UChar32 c, UChar32* decomp, long decompCount, bool fullComp) {
        unsigned long size = sizeof(decompCount) + 
		decompCount*sizeof(UChar32);
		
	if (s_ucCDDataSize+size > s_ucCDAllocSize){
		if (!s_ucCDData) {
			s_ucCDData = (char*)mmalloc(4096, 
						    "UnicodeProperties");
			if (!s_ucCDData)
				return log(LOG_WARN, "uni: "
					   "Out of Memory");
			s_ucCDAllocSize = 4096;			
			//dummy value for 0 index
			*(long*)s_ucCDData = 0xffffffff;
			s_ucCDDataSize = sizeof(long);
		}
		else {
			unsigned long newSize = s_ucCDAllocSize + 4096;
			char *newBuf = (char*)mrealloc(s_ucCDData, 
						       s_ucCDAllocSize,
						       newSize, 
						       "UnicodeProperties");
			if (!newBuf)
				return log(LOG_WARN, "uni: "
					   "Out of Memory");
			s_ucCDAllocSize = newSize;
			s_ucCDData = newBuf;
		}
		
	}
	// store fullComp flag in high bit of decompCount
	if (fullComp) 
		*(long*)(s_ucCDData+s_ucCDDataSize) = decompCount | 0x80000000;
	else
		*(long*)(s_ucCDData+s_ucCDDataSize) = decompCount;

	memcpy(s_ucCDData+s_ucCDDataSize+sizeof(decompCount), decomp, 
	       decompCount*sizeof(UChar32));
	long pos = s_ucCDDataSize;
	s_ucCDDataSize += size;
	
	return g_ucCDIndex.setValue(c, (void*)&pos);
}

// JAB: lazy engineer cut-n-paste job
UChar32 *getCDValue(UChar32 c, long *decompCount) {
	*decompCount = 0;
	long *pos = (long*)g_ucCDIndex.getValue(c);
	if (!pos || !*pos) return NULL;
	*decompCount = (*(long*)(&s_ucCDData[*pos])) & 0x7fffffff;
	return (UChar32*) (&s_ucCDData[*pos+sizeof(long)]);
}

// JAB: lazy engineer cut-n-paste job
long recursiveCDExpand(UChar32 c, UChar32 *buf, long bufSize) {
	long decompCount = 0;
	UChar32 *decomp = getCDValue(c, &decompCount);
	if (!decompCount) {
		buf[0] = c;
		return 1;
	}

	long decompIndex = 0;
	for (int i=0;i<decompCount;i++) {
		decompIndex += recursiveCDExpand(decomp[i], 
						 buf+decompIndex,
						 bufSize-decompIndex);
	}
	return decompIndex;
}

// JAB: we now have Kompatible and Canonical decomposition
bool saveKDecompTable(char *baseDir) {
	if (!s_ucKDData) return false;
	//char *filename = "ucdata/kd_data.dat";
	char filename[384];
	if (!baseDir) baseDir = ".";
	strcpy(filename, baseDir);
	strcat(filename, "/ucdata/kd_data.dat");
	size_t fileSize = s_ucKDDataSize;
	FILE *fp = fopen(filename, "w");
	if (!fp) {
		log(LOG_WARN, "uni: "
		    "Couldn't open %s for writing: %s", 
		       filename, strerror(errno));
		return false;
	}
	
	size_t nwrite = fwrite(s_ucKDData, fileSize, 1, fp);
	if (nwrite != 1) {
		log(LOG_WARN, "uni: Error writing %s "
		    "(filesize: %d)", 
		    filename, fileSize);
		fclose(fp);
		return false;
	}
	fclose(fp);
	strcpy(filename, baseDir);
	strcat(filename, "/ucdata/kdmap.dat");
	return saveUnicodeTable(&g_ucKDIndex, filename);
}

// JAB: lazy engineer cut-n-paste job
bool saveCDecompTable(char *baseDir) {
	if (!s_ucCDData) return false;
	//char *filename = "ucdata/cd_data.dat";
	char filename[384];
	if (!baseDir) baseDir = ".";
	strcpy(filename, baseDir);
	strcat(filename, "/ucdata/cd_data.dat");
	size_t fileSize = s_ucCDDataSize;
	FILE *fp = fopen(filename, "w");
	if (!fp) {
		log(LOG_WARN, "uni: "
		    "Couldn't open %s for writing: %s", 
		       filename, strerror(errno));
		return false;
	}
	
	size_t nwrite = fwrite(s_ucCDData, fileSize, 1, fp);
	if (nwrite != 1) {
		log(LOG_WARN, "uni: Error writing %s "
		    "(filesize: %d)", 
		    filename, fileSize);
		fclose(fp);
		return false;
	}
	fclose(fp);
	strcpy(filename, baseDir);
	strcat(filename, "/ucdata/cdmap.dat");
	return saveUnicodeTable(&g_ucCDIndex, filename);
}

// JAB: we now have Kompatible and Canonical decomposition
void resetDecompTables() {
	mfree(s_ucKDData, s_ucKDAllocSize, "UnicodeData");
	s_ucKDData = NULL;
	s_ucKDAllocSize = 0;
	s_ucKDDataSize = 0;
	g_ucKDIndex.reset();
	mfree(s_ucCDData, s_ucCDAllocSize, "UnicodeData");
	s_ucCDData = NULL;
	s_ucCDAllocSize = 0;
	s_ucCDDataSize = 0;
	g_ucCDIndex.reset();
}

// JAB: we now have Kompatible and Canonical decomposition
bool loadKDecompTable(char *baseDir) {
	if (s_ucKDData) {
		//reset table if already loaded
		resetDecompTables();
	}
	
	//char *filename = "ucdata/kd_data.dat";
	char filename[384];
	if (!baseDir) baseDir = ".";
	strcpy(filename, baseDir);
	strcat(filename, "/ucdata/kd_data.dat");

	FILE *fp = fopen(filename, "r");
	if (!fp) 
		return log(LOG_WARN, "uni: "
			   "Couldn't open %s for reading: %s",
			   filename, strerror(errno));
	fseek(fp,0,SEEK_END);
	size_t fileSize = ftell(fp);
	rewind(fp);
	char *buf = (char*)mmalloc(fileSize, "UnicodeProperties");
	if (!buf) {
		fclose(fp);
		return log(LOG_WARN, 
			   "uni: No memory to load %s", filename);
	}
	size_t nread = fread(buf, 1, fileSize, fp);
	if (nread != fileSize) {
		fclose(fp);
		mfree(buf, fileSize, "UnicodeProperties");
		return log(LOG_WARN, 
			   "uni: error reading %s", filename);
	}
	fclose(fp);
	
	strcpy(filename, baseDir);
	strcat(filename, "/ucdata/kdmap.dat");
	if (!loadUnicodeTable(&g_ucKDIndex, filename)) {
		mfree(buf, fileSize, "UnicodeProperties");
		return false;
	}
	s_ucKDData = buf;
	s_ucKDDataSize = nread;
	s_ucKDAllocSize = nread;
	return true;

}

// JAB: lazy engineer cut-n-paste job
bool loadCDecompTable(char *baseDir) {
	if (s_ucCDData) {
		//reset table if already loaded
		resetDecompTables();
	}
	
	//char *filename = "ucdata/cd_data.dat";
	char filename[384];
	if (!baseDir) baseDir = ".";
	strcpy(filename, baseDir);
	strcat(filename, "/ucdata/cd_data.dat");

	FILE *fp = fopen(filename, "r");
	if (!fp) 
		return log(LOG_WARN, "uni: "
			   "Couldn't open %s for reading: %s",
			   filename, strerror(errno));
	fseek(fp,0,SEEK_END);
	size_t fileSize = ftell(fp);
	rewind(fp);
	char *buf = (char*)mmalloc(fileSize, "UnicodeProperties");
	if (!buf) {
		fclose(fp);
		return log(LOG_WARN, 
			   "uni: No memory to load %s", filename);
	}
	size_t nread = fread(buf, 1, fileSize, fp);
	if (nread != fileSize) {
		fclose(fp);
		mfree(buf, fileSize, "UnicodeProperties");
		return log(LOG_WARN, 
			   "uni: error reading %s", filename);
	}
	fclose(fp);
	
	strcpy(filename, baseDir);
	strcat(filename, "/ucdata/cdmap.dat");
	if (!loadUnicodeTable(&g_ucCDIndex, filename)) {
		mfree(buf, fileSize, "UnicodeProperties");
		return false;
	}
	s_ucCDData = buf;
	s_ucCDDataSize = nread;
	s_ucCDAllocSize = nread;
	return true;

}

// JAB: we now have Kompatible and Canonical decomposition
bool loadDecompTables(char *baseDir) {
	return loadKDecompTable(baseDir) && loadCDecompTable(baseDir);
}
