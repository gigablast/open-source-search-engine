#include "gb-include.h"

#include "Unicode.h"
#include "UCWordIterator.h"

UCWordIterator::UCWordIterator() {

}
UCWordIterator::~UCWordIterator() {

}
bool UCWordIterator::setText(UChar* s, int32_t slen, int32_t version) {
	m_text = s;
	m_textLen = slen;
	m_last = s+slen;
	m_current = s;
	m_currentScript = ucScriptCommon;
	m_prevScript = ucScriptCommon;
	m_version = version;
	return true;
}
UChar *UCWordIterator::getText() {
	return m_text;
}
UChar32 UCWordIterator::currentCodePoint() {
	return m_currentCP;
}
int32_t UCWordIterator::current() {
	return m_current-m_text;
}
int32_t UCWordIterator::first() {
	m_current = m_text;
	m_done = false;
	m_currentCP = utf16EntityDecode(m_current, &m_next);
	m_currentScript = ucGetScript(m_currentCP);
	return 0;
}
int32_t UCWordIterator::next(){
	if (m_current >= m_last) {m_done=true; return -1;}
	m_current=m_next;
	bool latin1Clean = true;

	if(!ucIsWordChar(m_currentCP))
	{
		// non-word characters
		while (m_current < m_last){
			m_currentCP = utf16EntityDecode(m_current, &m_next);
			// new word starting here
			if (ucIsWordChar(m_currentCP)){
				m_currentScript = ucGetScript(m_currentCP);
				break;
			}
			if (ucIsIgnorable(m_currentCP)) {
				m_current = m_next;
				continue;
			}
			m_current = m_next;
		}
		return m_current - m_text;
	}
	// need to set initial value for latin1Clean
	// ...m_currentCP is set above while parsing previous
	// non-word characters
	latin1Clean = !(m_currentCP & 0xffffff80);

	while (m_current < m_last) {
		UChar32 temp = utf16EntityDecode(m_current, &m_next);
		// latin-1 quick case
		if (latin1Clean && !(temp & 0xffffff80)){
			m_currentScript = ucScriptCommon;
			if (is_alnum(temp)) {
				//m_prevScript = m_currentScript;
				//if (is_alpha(temp))
				//m_currentScript = ucScriptLatin;
				m_currentScript = ucGetScript(temp);
				m_prevCP = m_currentCP;
				m_currentCP = temp; 
				m_current=m_next;
				continue;
			}
		}
		
		latin1Clean = false;
		// we found a non-latin character, so for the 
		// rest of this word, we will do thorough checks
		UCProps props = ucProperties(temp);
		if (props & (UC_IGNORABLE|UC_EXTEND)){
			m_current = m_next;
			continue;
		}

		m_prevCP = m_currentCP;
		m_currentCP = temp; 

		// Well this is a pain in the ass...
		UChar32 extendChar = 0;
		if (m_version > 54){
			if  (m_currentCP == '+' ||
			     m_currentCP == '#') extendChar = m_currentCP;
		}
		else {
			if (m_currentCP == '+') extendChar = '+';
		}
		
		if (extendChar){
			UChar *p = m_next, *pnext = NULL;
			if (p < m_last) temp = utf16EntityDecode(p, &pnext);
			else temp = 0;
			if (p < m_last && ucIsWordChar(temp))
				break;
			// next char is not a word char either
			m_current = p;
			m_next = pnext;
			m_currentCP = temp;
			if (extendChar == '#') goto endWord;
			if (m_currentCP != '+') goto endWord;
			
			
			p=m_next;
			if (p < m_last) temp = utf16EntityDecode(p, &pnext);
			else temp = 0;

			if (p < m_last && ucIsWordChar(temp))
				goto endWord;
			m_current = p;
			m_next = pnext;
			m_currentCP = temp;
			goto endWord;
		}
		if (!(props&UC_WORDCHAR)){
			// reset script between words
		endWord:
			m_currentScript = ucScriptCommon;
			break;
		}
			
		// Break at ideographs and different scripts

		m_prevScript = m_currentScript;
		m_currentScript = ucGetScript(m_currentCP);
		if (props & ( UC_IDEOGRAPH | UC_HIRAGANA | UC_THAI )) 
			break;
		if (m_prevScript && m_currentScript &&
		    m_prevScript != m_currentScript)
			break;
			
		m_current = m_next;
	}
	return m_current - m_text;
}

int32_t UCWordIterator::last() {
	return m_last - m_text;
}
