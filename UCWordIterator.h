#ifndef UC_WORD_ITERATOR_H___
#define UC_WORD_ITERATOR_H___

#include "Unicode.h"
class UCWordIterator {
public:
	UCWordIterator();
	~UCWordIterator();
	bool setText(UChar* s, long slen, long version);
	UChar *getText();
	UChar32 currentCodePoint();

	// Set index to beginning of text
	long first();
	// find and return the index of the next word boundary
	long next();
	// end of text index
	long last();

	// current index
	long current();

	bool done() { return m_done; };
private:
	UChar *m_text;
	UChar *m_last;
	bool m_done;
	long m_textLen;
	UChar *m_current;
	UChar *m_next;

	UChar32 m_currentCP;
	UChar32 m_prevCP;
	UCScript m_currentScript;
	UCScript m_prevScript;

	UCProps m_currentProps;
	UCProps m_prevProps;
	
	long m_version; // titlerec version
};
#endif
