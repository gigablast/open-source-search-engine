#include "gb-include.h"

#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "Unicode.h"

static int32_t test_count = 10000;
int32_t elapsed_usec(const timeval* tv1, const timeval *tv2);
// fake shutdown for Loop and Parms
bool mainShutdown(bool urgent);
bool mainShutdown(bool urgent){return true;}
bool closeAll ( void *state , void (* callback)(void *state) ) { return true; }
bool allExit ( ) { return true; }

int main (int argc, char **argv) {
	char * filename = argv[1];
	fprintf(stderr, "Reading \"%s\"\n", filename);
	FILE *fp = fopen(filename,"r");
	if (!fp){
		fprintf(stderr, "Error: could not open file \"%s\"\n", 
			filename);
		exit(1);
	}
	
	//get charset
	char *charset = argv[2];

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
	
	int32_t ucBufSize = (int32_t)(nread*2.5);
	UChar *ucBuf = (UChar*)malloc(ucBufSize);
	int32_t ucLen = ucToUnicode(ucBuf, ucBufSize, file_buf, nread, 
				 "utf-8", NULL);
	
	struct timeval tv1, tv2;
	struct timezone tz1, tz2;

	int32_t times[test_count];
	int64_t total=0;
	int32_t max_time=-1L;
	int32_t min_time=999999999L;
	int32_t avg_time;

	//int32_t u8size = nread*2;
	//char *u8buf = (char*)malloc(u8size);
	int32_t newsize = 0;
	for (int i=0;i<test_count;i++ ){
		gettimeofday(&tv1, &tz1);
		newsize = ucToUnicode(ucBuf, ucBufSize, file_buf, nread, 
				      charset, NULL) << 1;
		gettimeofday(&tv2, &tz2);
		times[i] = elapsed_usec(&tv1, &tv2);
		total += times[i];
		if (times[i] < min_time) min_time = times[i];
		if (times[i] > max_time) max_time = times[i];
	}
	avg_time = total/test_count;

	fprintf(stderr,"ICU size: %"INT32", count: %"INT32", avg: %"INT32", min: %"INT32", max: %"INT32"\n",
		newsize, test_count, avg_time, min_time, max_time);
	int outfd = open("icu.out", O_CREAT|O_RDWR|O_TRUNC, 00666);
	if (outfd < 0) {printf("Error creating output file: %s\n", 
			       strerror(errno)); exit(1);}
	write(outfd, ucBuf, newsize);
	close(outfd);
#if 0
	total = 0; min_time = 999999999L; max_time = -1L;
	for (int i=0;i<test_count;i++ ){
		gettimeofday(&tv1, &tz1);
		//newsize = utf16ToUtf8_iconv(u8buf, u8size, ucBuf, ucLen);
		newsize = ucToUnicode_iconv(ucBuf, ucBufSize, file_buf, nread, 
					    charset, NULL) << 1;		
		gettimeofday(&tv2, &tz2);
		times[i] = elapsed_usec(&tv1, &tv2);
		total += times[i];
		if (times[i] < min_time) min_time = times[i];
		if (times[i] > max_time) max_time = times[i];
	}
	avg_time = total/test_count;

	fprintf(stderr,"iconv size: %"INT32", count: %"INT32", avg: %"INT32", min: %"INT32", max: %"INT32"\n",
		newsize, test_count, avg_time, min_time, max_time);
	outfd = open("iconv.out", O_CREAT|O_RDWR|O_TRUNC, 00666);
	if (outfd < 0) {printf("Error creating output file: %s\n", 
			       strerror(errno)); exit(1);}
	write(outfd, ucBuf, newsize);
	close(outfd);
#endif
#if 0
	total = 0; min_time = 999999999L; max_time = -1L;
	for (int i=0;i<test_count;i++ ){
		gettimeofday(&tv1, &tz1);
		newsize = utf16ToUtf8_intern(u8buf, u8size, ucBuf, ucLen);
		gettimeofday(&tv2, &tz2);
		times[i] = elapsed_usec(&tv1, &tv2);
		total += times[i];
		if (times[i] < min_time) min_time = times[i];
		if (times[i] > max_time) max_time = times[i];
	}
	avg_time = total/test_count;

	fprintf(stderr,"my size: %"INT32", count: %"INT32", avg: %"INT32", min: %"INT32", max: %"INT32"\n",
		newsize, test_count, avg_time, min_time, max_time);
	outfd = open("my.out", O_CREAT|O_RDWR|O_TRUNC, 00666);
	if (outfd < 0) {printf("Error creating output file: %s\n", 
			       strerror(errno)); exit(1);}
	write(outfd, u8buf, newsize);
	close(outfd);
#endif
	//printf("%s\n", u8buf);

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

