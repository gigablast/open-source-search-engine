#include "gb-include.h"

#include "Unicode.h"

int32_t elapsed_usec(const timeval* tv1, const timeval *tv2);
bool mainShutdown(bool urgent){return true;}
bool closeAll ( void *state , void (* callback)(void *state) ){return true;}
bool allExit ( ) {return true;}

int main(int argc, char **argv){
	if (argc < 2) {
		fprintf(stderr,"usage: %s filename [numTests]\n",argv[0]);
		exit(1);
	}
	if (!ucInit()){
		fprintf(stderr, "unable to initialize unicode tables\n");
		exit(1);
	}
	char * filename = argv[1];
	fprintf(stderr, "Reading \"%s\"\n", filename);
	FILE *fp = fopen(filename,"r");
	if (!fp){
		fprintf(stderr, "Error: could not open file \"%s\"\n", 
			filename);
		exit(1);
	}
	int32_t numTests = 1000;
	if (argc == 3) {
		numTests = atol(argv[2]);
	}
	printf("Running %"INT32" tests\n", numTests);

	// Get File size
	size_t file_size;
	fseek(fp, 0L, SEEK_END);
	file_size = (size_t)ftell(fp);
	fseek(fp, 0L, SEEK_SET);
	
	char *file_buf = (char*)malloc(file_size+1);
	size_t nread = fread(file_buf, (size_t)1,file_size, fp);
	fclose(fp);
	
	if (nread != file_size){
		fprintf(stderr, "Warning: wanted %d chars, but read %d\n",
			file_size, nread);
	}
	file_buf[nread] = '\0';
	
	struct timeval tv1, tv2;
	struct timezone tz1, tz2;
	printf("\nParsing with char*\n");
	int32_t totalCount = 0;
	int32_t alnumCount;
	int32_t punctCount;
	gettimeofday(&tv1, &tz1);
	for (int32_t i=0; i<numTests;i++){
		
		char *p = file_buf;
		alnumCount = 0;
		punctCount = 0;
		while(*p){
			if (is_alnum(*p) && !(*p & 0x80))
				alnumCount++;
			else punctCount++;
			totalCount++;
			p++;
		}
	}
	gettimeofday(&tv2, &tz2);
	printf("Done\n");
        int32_t elapsed = elapsed_usec(&tv1, &tv2);
	printf("%"INT32" total chars, %"INT32" usec, %"INT32" char/usec "
	       "(%"INT32" alnum, %"INT32" punct)\n", 
	       totalCount, elapsed, totalCount/elapsed,alnumCount, punctCount);

	printf("\nParsing with utf8Decode\n");
	totalCount = 0;
	gettimeofday(&tv1, &tz1);
	for (int32_t i=0; i<numTests;i++){
		
		char *p = file_buf;
		alnumCount = 0;
		punctCount = 0;
		while(*p){
			UChar32 c = utf8Decode(p, &p);
			//UChar32 c = *p;
			if (ucIsWordChar(c))
				alnumCount++;
			else punctCount++;
			totalCount++;
			//p++;
		}
	}
	gettimeofday(&tv2, &tz2);
	elapsed = elapsed_usec(&tv1, &tv2);
	printf("Done\n");
	printf("%"INT32" total chars, %"INT32" usec, %"INT32" char/usec "
	       "(%"INT32" alnum, %"INT32" punct)\n", 
	       totalCount, elapsed, totalCount/elapsed,alnumCount, punctCount);
}
int32_t elapsed_usec(const timeval* tv1, const timeval *tv2)
{
	int32_t sec_elapsed = (tv2->tv_sec - tv1->tv_sec);
	int32_t usec_elapsed = tv2->tv_usec - tv1->tv_usec;
	if (usec_elapsed<0){
		usec_elapsed += 1000000;
		sec_elapsed -=1;
	}
	usec_elapsed += sec_elapsed*1000000;
	return usec_elapsed;
}
