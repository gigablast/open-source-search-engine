#include "gb-include.h"

#include "Mime.h"
#include "Mem.h"

// . returns false if could not get a valid mime
// . we need the url in case there's a Location: mime that's base-relative
void Mime::set ( char *mime , int32_t mimeLen ) {
	m_mime    = mime;
	m_mimeLen = mimeLen;
	m_mimeEnd = mime + mimeLen;
}

char *Mime::getValue ( char *field , int32_t *valueLen ) {
	// caller's field length
	int32_t fieldLen = gbstrlen ( field );
	// parms to getLine()
	char *f    , *v;
	int32_t  flen ,  vlen;
	char *line = m_mime;
	// keep getting lines from the mime
	while ( ( line = getLine ( line , &f, &flen, &v , &vlen ) ) ) {
		if ( flen != fieldLen ) continue;
		if ( strncasecmp ( f , field , flen ) != 0 ) continue;
		*valueLen = vlen;
		return v;
	}
	// return NULL if no value found for "field"
	return NULL;
}

// . return ptr to next line to try
// . return NULL if no lines left
char *Mime::getLine ( char   *line  ,
		      char  **field , int32_t *fieldLen ,
		      char  **value , int32_t *valueLen ) {
	// reset field and value lengths
	*fieldLen = 0;
	*valueLen = 0;
	// a NULL line means the start
	if ( line == NULL ) line = m_mime;
	// a simple ptr
	char *p    = line;
	char *pend = m_mimeEnd;
 loop:
	// skip to next field (break on comment)
	while ( p < pend && *p!='#' && !is_alnum_a(*p) ) p++;
	// bail on EOF
	if ( p >= pend ) return NULL;
	// point to next line if comment
	if ( *p == '#' ) {
		while ( p < pend && *p != '\n' && *p !='\r' ) p++;
		// NULL on EOF
		if ( p >= pend ) return NULL;
		// try to get another line
		goto loop;
	}
	// save p's position
	char *s = p;
	// continue until : or \n or \r or EOF
	while ( p < pend && *p != ':' && *p != '\n' && *p !='\r' ) p++;
	// NULL on EOF
	if ( p >= pend ) return NULL;
	// return p for getting next line if no : found
	if ( *p != ':' ) goto loop;
	// set the field
	*field    = s;
	// set the field length
	*fieldLen = p - s;
	// reset value length to 0, in case we find none
	*valueLen = 0;
	// skip over :
	p++;
	// skip normal spaces and tabs at p now
	while ( p < pend && (*p==' ' || *p=='\t') ) p++;
	// NULL on EOF
	if ( p >= pend ) return NULL;
	// value is next
	*value = p;
	// goes till we hit \r or \n
	while ( p < pend && *p != '\n' && *p !='\r' ) p++;
	// set value length
	*valueLen = p - *value;
	// NULL on EOF
	if ( p >= pend ) return NULL;
	// otherwise, p is start of next line
	return p;
}
