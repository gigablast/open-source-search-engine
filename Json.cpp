#include "Json.h"
#include "SafeBuf.h"

class JsonItem *Json::addNewItem () {

	JsonItem *ji = (JsonItem *)m_sb.getBuf();

	if ( m_sb.m_length + (int32_t)sizeof(JsonItem) > m_sb.m_capacity ) {
		log("json: preventing buffer breach");
		return NULL;
	}

	// otherwise we got room
	m_sb.incrementLength(sizeof(JsonItem));


	if ( m_prev ) m_prev->m_next = ji;
	ji->m_prev = m_prev;
	ji->m_next = NULL;

	// we are the new prev now
	m_prev = ji;

	// value null for now
	ji->m_type = JT_NULL;
	// parent on stack
	JsonItem *parent = NULL;
	if ( m_stackPtr > 0 ) parent = m_stack[m_stackPtr-1];

	ji->m_parent = parent;
	
	// . if our parent was an array, we are an element in that array
	// . if it is an array of objects, then the name will be overwritten
	if ( parent ) { // && parent->m_type == JT_ARRAY ) {
		// inherit object name from parent
		ji->m_name    = parent->m_name;
		ji->m_nameLen = parent->m_nameLen;
	}

	return ji;
}

JsonItem *Json::getFirstItem ( ) {
	if ( m_sb.length() <= 0 ) return NULL;
	return (JsonItem *)m_sb.getBufStart();
}

JsonItem *Json::getItem ( char *name ) {
	JsonItem *ji = getFirstItem();
	// traverse the json
	for ( ; ji ; ji = ji->m_next ) {
		// just get STRINGS or NUMS
		if ( ji->m_type != JT_STRING && 
		     ji->m_type != JT_NUMBER &&
		     ji->m_type != JT_ARRAY ) 
			continue;
		// check name
		char *name2   = ji->m_name;
		if ( ! name2 ) return NULL; // array with empty name...
		if ( strcmp(name2,name) == 0 ) return ji;
	}
	return NULL;
}

#include "Mem.h" // gbstrlen()

JsonItem *Json::parseJsonStringIntoJsonItems ( char *json , int32_t niceness ) {

	m_prev = NULL;

	m_stackPtr = 0;
	m_sb.purge();

	JsonItem *ji = NULL;

	if ( ! json ) return NULL;

	// how much space will we need to avoid any reallocs?
	char *p = json;
	bool inQuote = false;
	int32_t need = 0;
	for ( ; *p ; p++ ) {
		// ignore any escaped char. also \x1234
		if ( *p == '\\' ) {
			if ( p[1] ) p++;
			continue;
		}
		if ( *p == '\"' )
			inQuote = ! inQuote;
		if ( inQuote ) 
			continue;
		if ( *p == '{' ||
		     *p == ',' ||
		     *p == '[' ||
		     *p == ':' )
			// +1 for null terminating string of each item
			need += sizeof(JsonItem) +1;
	}
	// plus the length of the string to store it decoded etc.
	need += p - json;
	// plus a \0 for the value and a \0 for the name of each jsonitem
	need += 2;
	// prevent cores for now
	need += 10;
	// . to prevent safebuf from reallocating do this
	// . safeMemcpy() calls reserve(m_length+len) and reserves
	//   tries to alloc m_length + (m_length+len) so since,
	//   m_length+len should never be more than "need" we need to
	//   double up here
	need *= 2;
	// this should be enough
	if ( ! m_sb.reserve ( need ) ) return NULL;
	// for testing if we realloc
	char *mem = m_sb.getBufStart();

	int32_t  size;

	char *NAME = NULL;
	int32_t  NAMELEN = 0;

	// reset p
	p = json;
	// json maybe bad utf8 causing us to miss the \0 char, so use "pend"
	char *pend = json + gbstrlen(json);

	// scan
	for ( ; p < pend ; p += size ) {
		// get size
		size = getUtf8CharSize ( p );

		// skip spaces
		if ( is_wspace_a (*p) )
			continue;

		// skip commas
		if ( *p == ',' ) continue;

		// did we hit a '{'? that means the existing json item
		// is a parent of the item(s) inside the {}'s
		if ( *p == '{' ) {
			// if ji is non-null it must be a name like in
			// \"stats\":{\"fetchTime\":2069,....}
			// . this indicates the start of a json object
			// . addNewItem() will push the current item on stack
			ji = addNewItem();
			if ( ! ji ) return NULL;
			// current ji is an object type then
			ji->m_type = JT_OBJECT;
			// set the name
			ji->m_name    = NAME;
			ji->m_nameLen = NAMELEN;
			ji->m_valueLen = 0;
			// this goes on the stack
			if ( m_stackPtr >= MAXJSONPARENTS ) return NULL;
			m_stack[m_stackPtr++] = ji;
			// and null this
			ji = NULL;
			continue;
		}
		// pop the stack?
		if ( *p == '}' ) {
			// just pop it and restore name cursor
			if ( m_stackPtr > 0 ) {
				JsonItem *px = m_stack[m_stackPtr-1];
				NAME    = px->m_name;
				NAMELEN = px->m_nameLen;
				m_stackPtr--;
			}
			continue;
		}
		// array of things?
		if ( *p == '[' ) {
			// make a newitem to put on stack
			ji = addNewItem();
			if ( ! ji ) return NULL;
			// current ji is an object type then
			ji->m_type = JT_ARRAY;
			// start of array hack. HACK!
			//ji->m_valueLong = (int32_t)p;
			ji->m_valueArray = p;
			// set the name
			ji->m_name    = NAME;
			ji->m_nameLen = NAMELEN;
			// init to a bogus value. should be set below.
			// at least this should avoid a core in XmlDoc.cpp
			// getTokenizedDiffbotReply()
			ji->m_valueLen = 0;
			// this goes on the stack
			if ( m_stackPtr >= MAXJSONPARENTS ) return NULL;
			m_stack[m_stackPtr++] = ji;
			ji = NULL;
			continue;
		}
		// pop the stack?
		if ( *p == ']' ) {
			// just pop it and restore name cursor
			if ( m_stackPtr > 0 ) {
				JsonItem *px = m_stack[m_stackPtr-1];
				NAME    = px->m_name;
				NAMELEN = px->m_nameLen;
				// start of array hack. HACK!
				char *start = (char *)px->m_valueArray;//Long;
				// include ending ']' in length of array
				px->m_valueLen = p - start + 1;
				m_stackPtr--;
			}
			continue;
		}

		// a quote?
		if ( *p == '\"' ) {
			// find end of quote
			char *end = p + 1;
			for ( ; *end ; end++ ) {
				// skip two chars if escaped
				if ( *end == '\\' && end[1] ) {
					end++; 
					continue;
				}
				// this quote is unescaped then
				if ( *end == '\"' ) break;
			}
			// field?
			char *x = end + 1;
			// skip spaces
			for ( ; *x && is_wspace_a(*x) ; x++ );
			// define the string
			char *str  = p + 1;
			int32_t  slen = end - str;
			// . if a colon follows, it was a field
			if ( *x == ':' ) {

				// we can't be the first thing in the safebuf
				// json must start with { or [ i guess
				// otherwise getFirstItem() won't work!
				if ( m_sb.m_length==0 ) {
					log("json: length is 0");
					g_errno = EBADJSONPARSER;
					return NULL;
				}

				// let's push this now so we can \0 term
				char *savedStr = m_sb.getBuf();
				m_sb.safeMemcpy ( str , slen );
				m_sb.pushChar('\0');
				// just set the name cursor
				NAME    = savedStr;//str;
				NAMELEN = slen;
			}
			// . otherwise, it was field value, so index it
			// . TODO: later make field names compounded to
			//   better represent nesting?
			// . added 'else if (NAME){' fix for json=\"too small\"
			else if ( NAME ) {
				// make a new one in safebuf. our
				// parent will be the array type item.
				ji = addNewItem();
				if ( ! ji ) return NULL;
				// we are a string
				ji->m_type = JT_STRING;
				// use name cursor
				ji->m_name    = NAME;
				ji->m_nameLen = NAMELEN;
				// get length decoded
				int32_t curr = m_sb.length();
				// store decoded string right after jsonitem
				if ( !m_sb.safeDecodeJSONToUtf8 (str,slen,
								 niceness ))
					return NULL;
				// store length decoded json
				ji->m_valueLen = m_sb.length() - curr;
				// end with a \0
				m_sb.pushChar('\0');
				// ok, this one is done
				ji = NULL;
			}
			else {
				log("json: fieldless name in json");
				g_errno = EBADJSONPARSER;
				return NULL;
			}
			// skip over the string
			size = 0;
			p    = x;
			continue;
		}

		// true or false?
		if ( (*p == 't' && strncmp(p,"true",4)==0) ||
		     (*p == 'f' && strncmp(p,"false",5)==0) ) {
			// make a new one
			ji = addNewItem();
			if ( ! ji ) return NULL;
			// copy the number as a string as well
			int32_t curr = m_sb.length();
			// what is the length of it?
			int32_t slen = 4;
			ji->m_valueLong = 1;
			ji->m_value64 = 1;
			ji->m_valueDouble = 1.0;
			if ( *p == 'f' ) {
				slen = 5;
				ji->m_valueLong = 0;
				ji->m_value64 = 0;
				ji->m_valueDouble = 0;
			}
			// store decoded string right after jsonitem
			if ( !m_sb.safeDecodeJSONToUtf8 (p,slen,niceness))
				return NULL;
			// store length decoded json
			ji->m_valueLen = m_sb.length() - curr;
			// end with a \0
			m_sb.pushChar('\0');
			ji->m_type = JT_NUMBER;
			// use name cursor
			ji->m_name    = NAME;
			ji->m_nameLen = NAMELEN;
			ji = NULL;
			// skip over the string
			size = 1;
			//p    = end;
			continue;
		}
			


		// if we hit a digit they might not be in quotes like
		// "crawled":123
		if ( is_digit ( *p ) ||
		     // like .123 ?
		     ( *p == '.' && is_digit(p[1]) ) ) {
			// find end of the number
			char *end = p + 1;
			// . allow '.' for decimal numbers
			// . TODO: allow E for exponent
			for ( ; *end && (is_digit(*end) || *end=='.');end++) ;
			// define the string
			char *str  = p;
			int32_t  slen = end - str;
			// make a new one
			ji = addNewItem();
			if ( ! ji ) return NULL;
			// back up over negative sign?
			if ( str > json && str[-1] == '-' ) str--;
			// decode
			//char c = str[slen];
			//str[slen] = '\0';
			ji->m_valueLong = atol(str);
			ji->m_value64 = atoll(str);
			ji->m_valueDouble = atof(str);
			// copy the number as a string as well
			int32_t curr = m_sb.length();
			// store decoded string right after jsonitem
			if ( !m_sb.safeDecodeJSONToUtf8 ( str, slen,niceness))
				return NULL;
			// store length decoded json
			ji->m_valueLen = m_sb.length() - curr;
			// end with a \0
			m_sb.pushChar('\0');
			//str[slen] = c;
			ji->m_type = JT_NUMBER;
			// use name cursor
			ji->m_name    = NAME;
			ji->m_nameLen = NAMELEN;
			ji = NULL;
			// skip over the string
			size = 0;
			p    = end;
			continue;
		}
	}

	// for testing if we realloc
	char *memEnd = m_sb.getBufStart();

	// bitch if we had to do a realloc. should never happen but i
	// saw it happen once, so do not core on that.
	if ( mem != memEnd )
		log("json: json parser reallocated buffer. inefficient.");

	// return NULL if no json items were found
	if ( m_sb.length() <= 0 ) return NULL;

	return (JsonItem *)m_sb.getBufStart();
}

	


void Json::test ( ) {

	char *json = "{\"tags\":[\"Apple Inc.\",\"Symbian\",\"IPad\",\"Music\"],\"summary\":\"Good timing and shrewd planning have played as much of a role as innovative thinking for the Silicon Valley juggernaut.\",\"icon\":\"http://www.onlinemba.com/wp-content/themes/onlinemba/assets/img/ico/apple-touch-icon.png\",\"text\":\"How did Apple rise through the ranks to become the world’s most profitable tech company? As it turns out, good timing and shrewd planning have played as much of a role as innovative thinking for the Silicon Valley juggernaut.For example, take the first MP3 player — MPMan, produced by South Korea-based SaeHan Information Systems. MPMan appeared in 1998, three years before the first iPods were released. As the original pioneer of portable MP3 player technology, SaeHan spent a good deal of time in court negotiating terms of use with various record companies. By 2001, a clear legal precedent was set for MP3 access — allowing Apple to focus less on courtroom proceedings and more on cutting-edge marketing campaigns for their new product."
		"When all else fails, they buy it: While iPads had fan boys salivating in the streets –the technology has been around for decades. One of the most obvious precursors to the iPad is FingerWorks, a finger gesture operated keyboard with a mouse very similar to Apple’s iPad controller. Fingerworks was bought in 2005 by none other than Apple – not surprisingly a couple years before the release of the iPhone and later the iPad.		 Of course, this isn’t to say that Apple doesn’t deserve to be the most valuable tech company in the world – just that innovation isn’t always about being first or best, sometimes, it’s just perception.\",\"stats\":{\"fetchTime\":2069,\"confidence\":\"0.780\"},\"type\":\"article\",\"meta\":{\"twitter\":{\"twitter:creator\":\"@germanny\",\"twitter:domain\":\"OnlineMBA.com\",\"twitter:card\":\"summary\",\"twitter:site\":\"@OnlineMBA_com\"},\"microdata\":{\"itemprop:image\":\"http://www.onlinemba.com/wp-content/uploads/2013/02/apple-innovates-featured-150x150.png\"},\"title\":\"3 Ways Apple Actually Innovates - OnlineMBA.com\",\"article:publisher\":\"https://www.facebook.com/OnlineMBAcom\",\"fb:app_id\":\"274667389269609\",\"og\":{\"og:type\":\"article\",\"og:title\":\"3 Ways Apple Actually Innovates - OnlineMBA.com\",\"og:description\":\"Good timing and shrewd planning have played as much of a role as innovative thinking for the Silicon Valley juggernaut.\",\"og:site_name\":\"OnlineMBA.com\",\"og:image\":\"http://www.onlinemba.com/wp-content/uploads/2013/02/apple-innovates-featured-150x150.png\",\"og:locale\":\"en_US\",\"og:url\":\"http://www.onlinemba.com/blog/3-ways-apple-innovates\"}},\"human_language\":\"en\",\"url\":\"http://www.onlinemba.com/blog/3-ways-apple-innovates\",\"title\":\"3 Ways Apple Actually Innovates\",\"textAnalysis\":{\"error\":\"Timeout during text analysis\"},\"html\":\"<div><div class=\\\"image_frame\\\"><img data-blend-adjustment=\\\"http://www.onlinemba.com/wp-content/themes/onlinemba/assets/img/backgrounds/bg.gif\\\" data-blend-mode=\\\"screen\\\" src=\\\"http://www.onlinemba.com/wp-content/uploads/2013/02/apple-innovates-invert-350x350.png\\\"></img></div><p>How did Apple rise"

		"\",\"supertags\":[{\"id\":856,\"positions\":[[7,12],[41,46],[663,668],[776,781],[1188,1193],[1380,1385],[1645,1650],[1841,1848],[2578,2583],[2856,2863],[2931,2936]],\"name\":\"Apple Inc.\",\"score\":0.8,\"contentMatch\":1,\"categories\":{\"1752615\":\"Home computer hardware companies\",\"27841529\":\"Technology companies of the United States\",\"33847259\":\"Publicly traded companies of the United States\",\"15168154\":\"Mobile phone manufacturers\",\"732736\":\"Retail companies of the United States\",\"9300270\":\"Apple Inc.\",\"23568549\":\"Companies based in Cupertino, "
		"California\",\"34056227\":\"Article Feedback 5\",\"37595560\":\"1976 establishments in California\",\"7415072\":\"Networking hardware companies\",\"699547\":\"Computer hardware companies\",\"37191508\":\"Software companies based in the San Francisco Bay Area\",\"855278\":\"Electronics companies\",\"5800057\":\"Steve Jobs\",\"7652766\":\"Display technology companies\",\"14698378\":\"Warrants issued in Hong Kong Stock Exchange\",\"4478067\":\"Portable audio player manufacturers\",\"31628257\":\"Multinational companies headquartered in the United States\",\"732825\":\"Electronics companies of the United States\",\"733759\":\"Computer companies of the United States\",\"6307421\":\"Companies established in 1976\"},\"type\":1,\"senseRank\":1,\"variety\":0.21886792452830184,\"depth\":0.6470588235294117},{\"id\":25686223,\"positions\":[[895,902],[2318,2325]],\"name\":\"Symbian\",\"score\":"
		"0.8,\"contentMatch\":0.9162303664921466,\"categories\":{\"33866248\":\"Nokia platforms\",\"20290726\":\"Microkernel-based operating systems\",\"39774425\":\"ARM operating systems\",\"2148723\":\"Real-time operating systems\",\"953043\":\"Smartphones\",\"10817505\":\"History of software\",\"17862682\":\"Mobile phone operating systems\",\"33569166\":\"Accenture\",\"2150815\":\"Embedded operating systems\",\"22533699\":\"Symbian OS\",\"22280474\":\"Mobile operating systems\"},\"type\":1,\"senseRank\":1,\"variety\":0.6566037735849057,\"depth\":0.6470588235294117},{\"id\":25970423,\"positions\":[[2639,2644],[2771,2775],[2864,2868]],\"name\":\"IPad\",\"score\":0.8,\"contentMatch\":1,\"categories\":{\"33578068\":\"Products introduced "
		"in 2010\",\"18083009\":\"Apple personal digital assistants\",\"23475157\":\"Touchscreen portable media players\",\"30107877\":\"IPad\",\"9301031\":\"Apple Inc. hardware\",\"27765345\":\"IOS (Apple)\",\"26588084\":\"Tablet computers\"},\"type\":1,\"senseRank\":1,\"variety\":0.49056603773584906,\"depth\":0.5882352941176471},{\"id\":18839,\"positions\":[[1945,1950],[2204,2209]],\"name\":\"Music\",\"score\":0.7,\"contentMatch\":1,\"categories\":{\"991222\":\"Performing arts\",\"693016\":\"Entertainment\",\"691484\":\"Music\"},\"type\":1,\"senseRank\":1,\"variety\":0.22264150943396221,\"depth\":0.7058823529411764}],\"media\":[{\"pixelHeight\":350,\"link\":\"http://www.onlinemba.com/wp-content/uploads/2013/02/apple-innovates-invert-350x350.png\",\"primary\":\"true\",\"pixelWidth\":350,\"type\":\"image\"}]}";


	int32_t niceness = 0;

	JsonItem *ji = parseJsonStringIntoJsonItems ( json , niceness );

	// print them out?
	//log("json: type0=%"INT32"",(int32_t)ji->m_type);
	// sanity test
	if ( ji->m_type != 6 ) { char *xx=NULL;*xx=0; }

	return;
}

bool JsonItem::getCompoundName ( SafeBuf &nameBuf ) {

	// reset, but don't free mem etc. just set m_length to 0
	nameBuf.reset();
	// get its full compound name like "meta.twitter.title"
	JsonItem *p = this;//ji;
	char *lastName = NULL;
	char *nameArray[20];
	int32_t  numNames = 0;
	for ( ; p ; p = p->m_parent ) {
		// empty name?
		if ( ! p->m_name ) continue;
		if ( ! p->m_name[0] ) continue;
		// dup? can happen with arrays. parent of string
		// in object, has same name as his parent, the
		// name of the array. "dupname":[{"a":"b"},{"c":"d"}]
		if ( p->m_name == lastName ) continue;
		// update
		lastName = p->m_name;
		// add it up
		nameArray[numNames++] = p->m_name;
		// breach?
		if ( numNames < 15 ) continue;
		log("build: too many names in json tag");
		break;
	}
	// assemble the names in reverse order which is correct order
	for ( int32_t i = 1 ; i <= numNames ; i++ ) {
		// copy into our safebuf
		if ( ! nameBuf.safeStrcpy ( nameArray[numNames-i]) ) 
			return false;
		// separate names with periods
		if ( ! nameBuf.pushChar('.') ) return false;
	}
	// remove last period
	nameBuf.removeLastChar('.');
	// and null terminate
	if ( ! nameBuf.nullTerm() ) return false;
	// change all :'s in names to .'s since : is reserved!
	char *px = nameBuf.getBufStart();
	for ( ; *px ; px++ ) if ( *px == ':' ) *px = '.';

	return true;
}

// is this json item in an array of json items?
bool JsonItem::isInArray ( ) {
	JsonItem *p = this;//ji;
	for ( ; p ; p = p->m_parent ) {
		// empty name? it's just a "value item" then, i guess.
		//if ( ! p->m_name ) continue;
		//if ( ! p->m_name[0] ) continue;
		if ( p->m_type == JT_ARRAY ) return true;
	}
	return false;
}

// convert numbers and bools to strings for this one
char *JsonItem::getValueAsString ( int32_t *valueLen ) {

	// strings are the same
	if ( m_type == JT_STRING ) {
		*valueLen = getValueLen();
		return getValue();
	}

	// numbers...
	// seems like when this overflowed when it was 64 bytes
	// it went into s_vbuf in Version.cpp
	static char s_numBuf[256];
	if ( (float)m_valueLong == m_valueDouble ) {
		*valueLen = snprintf ( s_numBuf,255,"%"INT32"", m_valueLong );
		return s_numBuf;
	}

	if ( (double)m_value64 == m_valueDouble ) {
		*valueLen = snprintf ( s_numBuf,255,"%"INT64"", m_value64 );
		return s_numBuf;
	}

	// otherwise return the number as it was written in the json
	// because it might have too many digits for printing as a double
	*valueLen = m_valueLen;
	return (char *)this + sizeof(JsonItem);

	// *valueLen = snprintf ( s_numBuf,255,"%f", m_valueDouble );
	// return s_numBuf;
}

bool endsInCurly ( char *s , int32_t slen ) {
	char *e = s + slen - 1;
	// don't backup more than 30 chars
	char *m = e - 30;
	if ( m < s ) m = s;
	// \0?
	if ( e > m && *e == '\0' ) e--;
	// scan backwards, skipping whitespace
	for ( ; e > m && is_wspace_a(*e) ; e-- );
	// should be a } now to be valid json
	if ( e >= m && *e == '}' ) return true;
	return false;
}


// Accepts a json string which has a top level object and a "key":val pair
// return false unless jsonStr has the new key:val
bool Json::prependKey(SafeBuf& jsonStr, char* keyVal) {
	int32_t ndx = jsonStr.indexOf('{');
	// no object? try array? fail for now
	if( ndx == -1  || ndx == jsonStr.length() - 1 ) return false;
	ndx++; //the insert pos
	if(ndx == jsonStr.length()) return false;

	// find if the object had any other keys
	int32_t jsonStrLen = jsonStr.length();
	int32_t i = ndx;
	while(i < jsonStrLen && isspace(jsonStr[i])) i++;
	if( i == jsonStrLen ) return false;


	
	if (jsonStr[i] != '}') {
		jsonStr.insert(",\n", i);
	} //else we are the only item, no comma

	return jsonStr.insert(keyVal, ndx);


}


// bool Json::printToString(SafeBuf& out, JsonItem* ji = NULL) {
// 	if(!ji) ji = getFirstItem();

// 	for ( ; ji ; ji = ji->m_next ) {
// 		switch (ji->m_type) {
// 		case JT_NULL:
// 			out.safeMemcpy("null", 4);
// 		break;
// 		case JT_NUMBER:
// 			int32_t vl;
// 			char* v = ji->getValueAsString(&vl);
// 			out.safeMemcpy(v, vl);
// 			break;
// 		case JT_STRING:
// 			int32_t vl;
// 			char* v = ji->getValueAsString(&vl);
// 			out.pushChar('"');
// 			out.safeMemcpy(v, vl);
// 			out.pushChar('"');
// 		break;
// 		case JT_ARRAY:
// 			// wha? really? I would've thought this would contain 
// 			// jsonitems and not a string
// 			safeMemcpy(ji->m_valueArray, ji->m_valueArray);
// 		break;
// 		case JT_OBJECT:
// 			out.pushChar('{');
// 			out.safeMemcpy(v, vl);
// 			out.pushChar("\"");
// 		break;
// 		}
// 	}
// 	out->
// }
