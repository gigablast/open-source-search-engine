#include "hash.h"

int main(int argc, char **argv) {
	
	char *str1 = "Apple";
	char *str2 = "Candy";
	char *str3 = "AppleCandy";

	int64_t h1, h2, h3,h4;
	hashinit();
	h1 = hash64Lower(str1, gbstrlen(str1));
	h2 = hash64Lower(str2, gbstrlen(str2));
	printf("h1: %lld, h2: %lld\n", h1, h2);

	h3 = hash64Lower(str3, gbstrlen(str3));
	h4 = hash64Lower(str2, gbstrlen(str2),h1);
	printf("h3: %lld, h4: %lld\n", h3,h4);
	
	int64_t h5;
	h5 = h1^h2;
	printf("h5: %lld\n", h5);

	int64_t h6;
	h6 = hash64(h1,h2);
	printf("h6: %lld\n", h6);
}
