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
int32_t ucNormalizeNFKC(UChar *outBuf, int32_t outBufSize,
		     UChar *inBuf, int32_t inBufSize, bool strip = false);

// Decomposed Kompatibility Form
int32_t ucNormalizeNFKD(UChar *outBuf, int32_t outBufSize,
		 UChar *inBuf, int32_t inBufSize);

bool initCompositionTable();
void resetCompositionTable() ;

#endif
