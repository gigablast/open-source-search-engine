#include "gb-include.h"

//#include <unicode/unorm.h>
#include "Unicode.h"


bool mainShutdown ( bool urgent ) ;
bool mainShutdown ( bool urgent ) {return true;}

// Test first 255 chars of unicode (iso-8859-1) for normalization
int main(int argc, char*argv){
	ucInit();
	int32_t count = 0;
	for (UChar32 c = 0; c < 0x10000; c++){
		//UErrorCode err = U_ZERO_ERROR;
		//bool isNorm = unorm_isNormalized(&c, 
		//				 1, UNORM_NFKC,&err);
		//if (U_FAILURE(err)) printf("0x%02x: Error: %s\n", 
		//			   c, u_errorName(err));
		//else// if (!isNorm)
		//	printf("0x%02x(%c): %s %s\n", c,c,
		//	       isNorm?"Normal":"NOT Normal",
		//	       is_alnum((char)c)?"":"not alnum");
		if (ucIsWhiteSpace(c)){
			count++;
			printf("0x%02x (%c): whitespace\n", c, c);
		}
	}
	printf("Count: %d\n", count);
}
