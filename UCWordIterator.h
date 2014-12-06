#ifndef UC_WORD_ITERATOR_H___
#define UC_WORD_ITERATOR_H___

#include "Unicode.h"
class UCWordIterator {
public:
	UCWordIterator();
	~UCWordIterator();
	bool setText(UChar* s, int32_t slen, int32_t version);
	UChar *getText();
	UChar32 currentCodePoint();

	// Set index to beginning of text
	int32_t first();
	// find and return the index of the next word boundary
	int32_t next();
	// end of text index
	int32_t last();

	// current index
	int32_t current();

	bool done() { return m_done; };
private:
	UChar *m_text;
	UChar *m_last;
	bool m_done;
	int32_t m_textLen;
	UChar *m_current;
	UChar *m_next;

	UChar32 m_currentCP;
	UChar32 m_prevCP;
	UCScript m_currentScript;
	UCScript m_prevScript;

	UCProps m_currentProps;
	UCProps m_prevProps;
	
	int32_t m_version; // titlerec version
};
#endif
