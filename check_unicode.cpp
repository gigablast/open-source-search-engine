#include <stdio.h>
#include <stdlib.h>
#include "iconv.h"

#define BUFSIZE 1024*1024
int main(int argc, char **argv){
	char inbuf[BUFSIZE];

	iconv_t conv = iconv_open("UTF-8", "UTF-8");
	char *dummy = NULL;
	size_t dummy2 = 0;
	// reset convertor
	iconv(conv,NULL,NULL,&dummy,&dummy2);
	size_t inCursor = 0;

	while (!feof(stdin)){
		char outbuf[BUFSIZE];
		int count = fread(inbuf, 1, BUFSIZE, stdin);
		char *pin = inbuf;
		char *pout = outbuf;
		size_t incount = count;
		size_t outcount = BUFSIZE;
		
		int res = iconv(conv, &pin, &incount, &pout, &outcount);
		if (res < 0) {
			switch(errno) {
			case EILSEQ:
				printf("Illegal sequence: 0x%02x 0x%02x 0x%02x 0x%02x "
				       "at byte %d.\n", 
				       (unsigned char)*pin, 			
				       (unsigned char)*(pin+1),
 				       (unsigned char)*(pin+2), 
 				       (unsigned char)*(pin+3), 
				       pin-inbuf+inCursor);
				break;
			case EINVAL:
				printf("Invalid character: 0x%02x at byte %d.\n", 
				       (unsigned char)*pin, pin-inbuf+inCursor);
				break;
			}
		}
			
		
	}

}
