#ifndef UCNORMALIZER_H___
#define UCNORMALIZER_H___

#define UC_KOMPAT_MASK 1
#define UC_COMPOSE_MASK 2

enum UCNormForm {
	ucNFD  = 0,
	ucNFC  = UC_COMPOSE_MASK,
	ucNFKD = UC_KOMPAT_MASK,
	ucNFKC = (UC_KOMPAT_MASK|UC_COMPOSE_MASK)
};


// Combined Kompatibility Form
long ucNormalizeNFKC(UChar *outBuf, long outBufSize,
		     UChar *inBuf, long inBufSize, bool strip = false);

// Decomposed Kompatibility Form
long ucNormalizeNFKD(UChar *outBuf, long outBufSize,
		 UChar *inBuf, long inBufSize);

bool initCompositionTable();
void resetCompositionTable() ;

#endif
