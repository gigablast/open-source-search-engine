#include "gb-include.h"

#include "Unicode.h"
#include "Words.h"
#include <sys/time.h>

#define NUM_TEST_RUNS 1000

// cmd line flags
enum {
	flNone = 0,
	flEncoding,
	flParser,
	flHash,
	flFilterSpaces,
};

int32_t elapsed_usec(const timeval* tv1, const timeval *tv2);
void parse_doc_8859_1(char *s, int len, bool doHash = false,char *charset=NULL);
//void parse_doc_utf8(char *s, int len, char *charset=NULL);
void parse_doc_icu(char *s, int len, bool doHash=false,char *charset=NULL);
int32_t time_parser(void (*)(char*,int,bool,char*), char*, int, bool doHash=false,char *charset=NULL, int test_count = 1);
//void PrintTokens(UChar **tokens, int num_tokens, int32_t *toklen, bool ascii=false, bool html=false);
// Read unicode from a file and parse into words

// fake shutdown for Loop and Parms
bool mainShutdown(bool urgent);
bool mainShutdown(bool urgent){return true;}

bool doFilterSpaces = false;

int main(int argc, char *argv[])
{
	char *encoding = NULL;
	bool doHash = false;

	if (argc < 2){
		fprintf(stderr, "Usage: %s [ -e encoding ] filename [ [ -e encoding] filename2 ] ...\n", argv[0]);
		exit(1);
	}
	ucInit();
	if ( ! hashinit() ) {
		log("db: Failed to init hashtable." ); return 1; }
	// . hashinit() calls srand() w/ a fixed number
	// . let's mix it up again
	srand ( time(NULL) );

	int i;
	int flag = flNone;
	void (*parser)(char*,int,bool,char*) = NULL;
	char *parser_str = "";
	for (i=1;i<argc;i++){

		// Read cmdline args
		if (argv[i][0] == '-'){
			if (gbstrlen(argv[i])<2){
				fprintf(stderr, "Unknown argument: %s\n", 
					argv[i]);
				exit(1);
			}
			switch(argv[i][1]){
			case 'e':
				flag = flEncoding;
				break;
			case 'p':
				flag = flParser;
				break;
			case 'h':
				flag = flHash;
				break;
			case 's':
				flag = flFilterSpaces;
				break;
			default:
				fprintf(stderr, "Unknown flag: %s\n",
					argv[i]);
				exit(1);
			}
			continue; //next arg
		}
		
		// Switch default encoding
		if (flag == flEncoding){
			encoding = argv[i];
			flag=flNone;
			fprintf(stderr, "Using encoding: %s\n", encoding);
			continue;
		}
		// switch parser
		if (flag == flParser){
			if (!strncmp(argv[i], "icu", 3)){
				parser = parse_doc_icu;
				parser_str = "ICU BreakIterator";
			}
			else {
				parser = parse_doc_8859_1;
				parser_str = "iso-8859-1";
			}
			flag=flNone;
			fprintf(stderr, "Using parser: %s\n", parser_str);
			continue;
		}
		if (flag == flHash){
			if ((!strncmp(argv[i], "0", 1)) ||
			    (!strncmp(argv[i], "f", 1))){
				doHash = false;
			}
			else{ doHash = true; }
			flag = flNone;
			continue;
		}
		if (flag == flFilterSpaces){
			if ((!strncmp(argv[i], "0", 1)) ||
			    (!strncmp(argv[i], "f", 1))){
				doFilterSpaces = false;
			}
			else{ doFilterSpaces = true; }
			flag = flNone;
			continue;
		}

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
		size_t nread = fread(file_buf, (size_t)1,file_size, fp);
		fclose(fp);

		if (nread != file_size){
			fprintf(stderr, "Warning: wanted %d chars, but read %d\n",
				file_size, nread);
		}
		file_buf[nread] = '\0';
	       
		//struct timeval tv1, tv2;
		//struct timezone tz1, tz2;
		int32_t usec_elapsed;
//		int testnum;
		
		char ucBuf[128*1024];
		int32_t ucLen = ucToUnicode((UChar*)ucBuf,128*1024,
					 file_buf,nread+1,"utf-8", 10);
		ucLen <<= 1;

		
		usec_elapsed = time_parser(parser,
					   file_buf,nread,doHash,
					   encoding,
					   NUM_TEST_RUNS);
		fprintf(stderr,"Document parsed (%s, hash=%s, filterSpaces=%s): %"INT32" usec\n", 
			parser_str, 
			doHash?"true":"false",
			doFilterSpaces?"true":"false",
			usec_elapsed);
	}
	fprintf(stderr, "Done\n");
	return 0;
}

int32_t time_parser(void (*parse_doc)(char*,int, bool,char*), char* buf, int len, bool doHash, char *charset, int test_count)
{
	struct timeval tv1, tv2;
	struct timezone tz1, tz2;

	int32_t times[test_count];
	int64_t total=0;
	int32_t max_time=-1;
	int32_t min_time=999999999;
	for (int i=0;i<test_count;i++ ){
		gettimeofday(&tv1, &tz1);
		parse_doc(buf,len, doHash,charset);
		gettimeofday(&tv2, &tz2);
		times[i] = elapsed_usec(&tv1, &tv2);
		total += times[i];
		if (times[i] < min_time) min_time = times[i];
		if (times[i] > max_time) max_time = times[i];
	}
	int32_t avg_time = total/test_count;
	printf("Hash %s, count: %d, avg: %"INT32", min: %"INT32", max: %"INT32"\n",
	       (doHash?"true":"false"),
	       test_count, avg_time, min_time, max_time);
	return avg_time;	
}


void parse_doc_8859_1(char *s, int len, bool doHash,char *charset)
{
	Xml xml;
	xml.set(csASCII,s,len,false, 0, false, TITLEREC_CURRENT_VERSION);
	//fprintf(stderr,"\nparse_doc_8859_1\n");

	// Extract text from (x)html
	char *text_buf = (char*)malloc(len+1);
	xml.getText(text_buf, 
		    len, 
		    0,
		    99999999,
		    false,
		    true,
		    false,
		    doFilterSpaces,
		    false);
	Words words;

	// just tokenize words
	words.set(false, text_buf, TITEREC_CURRENT_VERSION, doHash);
	free(text_buf);
}


//////////////////////////////////////////////////////////////////
void parse_doc_icu(char *s, int len, bool doHash, char *charset){
	Xml xml;
	xml.set(csUTF8,s,len,false, 0,false, TITLEREC_CURRENT_VERSION);
	//fprintf(stderr,"\nparse_doc_icu\n");	
	// Extract text from (x)html
	char *text_buf = (char*)malloc(64*1024);
	int32_t textLen = xml.getText(text_buf, 
				   64*1024, 
				   0,
				   99999999,
				   false,
				   true,
				   false,
				   doFilterSpaces,
				   false);
	Words w;
	w.set(true,false, text_buf, textLen, TITLEREC_CURRENT_VERSION,doHash);
	free(text_buf);
}

/////////////////////////////////////////////////////////////////
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

#if 0
void PrintTokens(UChar **tokens, int num_tokens, int32_t *toklen, bool ascii, bool html)
{
	if (html) printf("<html>\n<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\">\n<head>\n<style> .token { border: 1px solid gray;background: #f0ffff;}</style>\n</head>\n<body>\n");
	for (int i=0;i<num_tokens;i++){
		if (html) printf("<span class=\"token\">");
		else printf("Token %d: ",i);

		UChar *tok = tokens[i];
		int tlen = toklen[i];

		// Doesn't account for 4-byte unicode yet
		for (int j=0;j<tlen;j++){
			if (ascii){
				if (tok[j]>=0x20 && tok[j] < 0x80)
					printf("%c", (char)tok[j]);
				else
					printf("[u+%02x]",(int)tok[j]); 
			}
			else{
				char code_point[4];
				int num_bytes = utf8_encode((u_int32_t)tok[j],
							   code_point);
				if (html){
					if (code_point[0] == '<') printf("&lt;");
					else if (code_point[0] == '>') printf("&gt;");
					else if (code_point[0] == '&') printf("&amp;");
					else {
						for(int k=0;k<num_bytes;k++){
							putchar(code_point[k]);
						}
					}
				}
				else
					for(int k=0;k<num_bytes;k++){
						putchar(code_point[k]);
					}

			}
		}
		if (html) printf("</span>");
		printf("\n");

		
	}
	if (html) printf("</body>\n</html>\n");
	
}
#endif
