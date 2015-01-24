#include "gb-include.h"

#include "Unicode.h"
#include "Words.h"
//#include "Tokens.h"
#include <sys/time.h>


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

// Read unicode from a file and parse into words
int main(int argc, char**argv)
{
	if (argc < 2){
		fprintf(stderr, "Usage: %s filename ...\n", argv[0]);
		exit(1);
	}
	init_unicode();
	if ( ! hashinit() ) {
		log("db: Failed to init hashtable." ); return 1; }
	// . hashinit() calls srand() w/ a fixed number
	// . let's mix it up again
	srand ( time(NULL) );

	int i;
	for (i=1;i<argc;i++){
		char * filename = argv[i];
		fprintf(stderr, "Reading \"%s\"\n", filename);
		FILE *fp = fopen(filename,"r");
		if (!fp){
			fprintf(stderr, "Error: could not open file \"%s\"\n", 
				filename);
			continue;
		}
		// Get File size
		size_t file_size;
		fseek(fp, 0L, SEEK_END);
		file_size = (size_t)ftell(fp);
		fseek(fp, 0L, SEEK_SET);
		
		char *file_buf = (char*)malloc(file_size+1);
		char *text_buf = (char*)malloc(file_size+1);
		size_t nread = fread(file_buf, (size_t)1,file_size, fp);
		fclose(fp);

		if (nread != file_size){
			fprintf(stderr, "Warning: wanted %d chars, but read %d\n",
				file_size, nread);
		}
		file_buf[nread] = '\0';

		//utf8_parse_buf(file_buf);
		Xml xml;
		xml.set(file_buf,nread,false, false);

		struct timeval tv1, tv2;
		struct timezone tz1, tz2;

		int foo;
		

		
		// Extract text from (x)html
		int32_t textlen = xml.getText(text_buf, 
					   nread, 
					   0,
					   99999999,
					   false,
					   true,
					   true,
					   true,
					   false);
#define NUM_RUNS 1
		///////////////////////////////////////
		// Parse buffer the old way first for baseline comparision
		Words words;
		gettimeofday(&tv1, &tz1);
		// just tokenize words
		for(foo=0;foo<NUM_RUNS;foo++){
			words.set(false, text_buf, TITLEREC_CURRENT_VERSION,
				  false);
		}
		gettimeofday(&tv2, &tz2);
		int32_t usec_elapsed = elapsed_usec(&tv1, &tv2);

		printf("\nDocument parsed (iso-8851-1): %"INT32" usec (%"INT32" words)\n", 
		       usec_elapsed,
		       words.getNumWords());
		
		///////////////////////////////////////
		// Parse buffer the new way 

		Tokens tokens;
		gettimeofday(&tv1, &tz1);
		// just tokenize words
		for(foo=0;foo<NUM_RUNS;foo++){
			tokens.set(text_buf, false);
		}
		//int32_t count = utf8_count_words(file_buf);
		gettimeofday(&tv2, &tz2);
		usec_elapsed = elapsed_usec(&tv1, &tv2);

		printf("\nDocument parsed (Unicode): %"INT32" usec (%"INT32" words)\n", 
		       usec_elapsed,
		       tokens.getNumTokens());
		int32_t max_words = words.getNumWords();
		if (tokens.getNumTokens() > max_words)
			max_words = tokens.getNumTokens();
		//
		// Print tokenization side by side
		for (foo=0;foo<max_words;foo++){
			printf("%5d: ", foo);
			if (foo<words.getNumWords()){
				int n;
				char *s;
				s = words.getWord(foo);
				for(n=0;n<words.getWordLen(foo);n++){
					unsigned char c = s[n];
					if (c == '\n') 
						printf("[\\n]");
					else if ((c>=0x20) && ((unsigned)c<=0x7f)){
						//putchar(c);
						printf("%4c", (unsigned char)c);
					}
					else{
						printf("<%02lX>", (u_int32_t)c);
					}
				}
				for(n=words.getWordLen(foo);n<15;n++)
					printf("    ");
			}
			else{
				printf("%60s", "");
			}
			
			printf(" | ");
			if (foo<tokens.getNumTokens()){
				char *s;
				s = tokens.getToken(foo);
				char *pp;
				for(pp=s;(pp-s)<tokens.getTokenLen(foo);){
					u_int32_t c = utf8_read(pp,&pp);
					if (c == (u_int32_t)'\n') 
						printf("\\n");
					else
						utf8_putchar(c);
				}
			}
			putchar('\n');

			
		}
	}
	fprintf(stderr, "Done\n");
}



