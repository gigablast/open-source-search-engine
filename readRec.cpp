#include "gb-include.h"

#include "zlib.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


//g++ -g readRec.cpp ../lib/libz.a ../lib/libstdc++-libc6.2-2.a.3 ../lib/libstdc++.a -o readrec




int main ( int argc , char *argv[] ) {
	//	char rbuf[16777216];
	long recSize;
	char* rbuf = NULL;
	long rbufSize = 0;
	char* cbuf = NULL;
	long cbufSize = 0;
	long printUpdates = 0;
	long startOffset = 0;
	long totalRead = 0;
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
		int bytesRead = read(fileno,  &recSize, sizeof(long));
		if(bytesRead == 0 || recSize == 0) {
			fprintf(stderr, "done.\n");
			exit(1);
		}

		if(recSize > rbufSize) {
			char* tmpBuf = (char*)realloc(rbuf, recSize);
			if(!tmpBuf) {
				fprintf(stderr, "no memory, needed %li.", recSize);
				exit(1);
			}
			rbuf = tmpBuf;
		}
		long bytesRead2 = read(fileno, rbuf, recSize);
		if (bytesRead2 != recSize) {
			printf("couldn't read %li", recSize);
			exit(1);
		}

		char* p = rbuf;
		char* url = p;
		long urlLen = gbstrlen(url);
		p += urlLen + 1;
		long docSize = *(long*)p; 
		p += sizeof(long);
		long compressedDocSize = *(long*)p; 
		p += sizeof(long);

		long need = docSize + urlLen + 64;
		if( need > cbufSize) {
			char* tmpBuf = (char*)realloc(cbuf, need);
			if(!tmpBuf) {
				fprintf(stderr, "no memory, needed %li", need);
				exit(1);
			}
			cbuf = tmpBuf;
			cbufSize = need;
		}
		char* writeBuf = cbuf;
		long writeLen = sprintf(writeBuf, "%s\n%li\n", url, docSize);
		writeBuf += writeLen;
		unsigned long destLen = docSize;
		int stat = uncompress((unsigned char*)writeBuf, 
				      &destLen, 
				      (unsigned char*)p, 
				      compressedDocSize);


		if(stat != Z_OK) fprintf(stderr, "bad record.");
		p += compressedDocSize;

		//fprintf(stdout, "%s\n%li\n", url, docSize);
		write(STDOUT_FILENO, cbuf, writeLen+destLen);
		if(printUpdates) {
			totalRead += bytesRead + bytesRead2;
			fprintf(stderr, "%li bytes read...\n",totalRead);
		}
	}
}
