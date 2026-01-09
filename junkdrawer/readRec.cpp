#include "gb-include.h"

#include "zlib.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


//g++ -g readRec.cpp ../lib/libz.a ../lib/libstdc++-libc6.2-2.a.3 ../lib/libstdc++.a -o readrec




int main ( int argc , char *argv[] ) {
	//	char rbuf[16777216];
	int32_t recSize;
	char* rbuf = NULL;
	int32_t rbufSize = 0;
	char* cbuf = NULL;
	int32_t cbufSize = 0;
	int32_t printUpdates = 0;
	int32_t startOffset = 0;
	int32_t totalRead = 0;
	if(argc < 1)  {
		fprintf(stderr, "usage: readRecs [filename] [printUpdates:0|1] [startoffset]");
		exit(1);
	}

	if(argc > 2)  {
		printUpdates = atol(argv[2]);
	}

	if(argc > 3)  {
		startOffset = atol(argv[3]);
	}

	int fileno = open(argv[1], O_RDONLY/*|O_LARGEFILE*/);

	if(fileno < 0) {
		fprintf(stderr, "bad file: %s ", argv[1]);
		exit(1);
	}

	if(startOffset) lseek(fileno, startOffset, SEEK_SET);

	while(1) {
		//read the record size
		int bytesRead = read(fileno,  &recSize, sizeof(int32_t));
		if(bytesRead == 0 || recSize == 0) {
			fprintf(stderr, "done.\n");
			exit(1);
		}

		if(recSize > rbufSize) {
			char* tmpBuf = (char*)realloc(rbuf, recSize);
			if(!tmpBuf) {
				fprintf(stderr, "no memory, needed %"INT32".", recSize);
				exit(1);
			}
			rbuf = tmpBuf;
		}
		int32_t bytesRead2 = read(fileno, rbuf, recSize);
		if (bytesRead2 != recSize) {
			printf("couldn't read %"INT32"", recSize);
			exit(1);
		}

		char* p = rbuf;
		char* url = p;
		int32_t urlLen = gbstrlen(url);
		p += urlLen + 1;
		int32_t docSize = *(int32_t*)p; 
		p += sizeof(int32_t);
		int32_t compressedDocSize = *(int32_t*)p; 
		p += sizeof(int32_t);

		int32_t need = docSize + urlLen + 64;
		if( need > cbufSize) {
			char* tmpBuf = (char*)realloc(cbuf, need);
			if(!tmpBuf) {
				fprintf(stderr, "no memory, needed %"INT32"", need);
				exit(1);
			}
			cbuf = tmpBuf;
			cbufSize = need;
		}
		char* writeBuf = cbuf;
		int32_t writeLen = sprintf(writeBuf, "%s\n%"INT32"\n", url, docSize);
		writeBuf += writeLen;
		uint32_t destLen = docSize;
		int stat = uncompress((unsigned char*)writeBuf, 
				      &destLen, 
				      (unsigned char*)p, 
				      compressedDocSize);


		if(stat != Z_OK) fprintf(stderr, "bad record.");
		p += compressedDocSize;

		//fprintf(stdout, "%s\n%"INT32"\n", url, docSize);
		write(STDOUT_FILENO, cbuf, writeLen+destLen);
		if(printUpdates) {
			totalRead += bytesRead + bytesRead2;
			fprintf(stderr, "%"INT32" bytes read...\n",totalRead);
		}
	}
}
