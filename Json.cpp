#include "Json.h"

class JsonItem *Json::addNewItem () {

	// current item becomes parent, if any.... not if brother in array
	//if ( m_ji ) {
	//	// store the OFFSET on the stack since we realloc m_buf
	//	m_stack[m_stackPtr++] = m_ji;
	//}


	JsonItem *prev = m_ji;

	m_ji = (JsonItem *)m_sb.getBuf();
	m_sb.incrementLength(sizeof(JsonItem));

	prev->m_next = m_ji;
	m_ji->m_prev = prev;

	// value null for now
	m_ji->m_type = JT_NULL;
	// parent on stack
	JsonItem *parent = NULL;
	if ( m_stackPtr > 0 ) parent = m_stack[m_stackPtr-1];

	m_ji->m_parent = parent;
	
	// if our parent was an array, we are an element in that array
	if ( parent && parent->m_type == JT_ARRAY ) {
		// inherit object name from parent
		m_ji->m_name    = parent->m_name;
		m_ji->m_nameLen = parent->m_nameLen;
	}

	return m_ji;
}


JsonItem *Json::parseJsonStringIntoJsonItems ( SafeBuf *sb , char *json ) {

	m_stackPtr = 0;
	m_ji = NULL;

	// how much space will we need to avoid any reallocs?
	char *p = json;
	bool inQuote;
	long need = 0;
	for ( ; *p ; p++ ) {
		if ( *p == '\"' && (p==json || p[-1]!='\\') )
			inQuote = ! inQuote;
		if ( inQuote ) continue;
		if ( *p == '{' ||
		     *p == ',' ||
		     *p == '[' ||
		     *p == ':' )
			need += sizeof(JsonItem);
	}
	// plus the length of the string to store it decoded etc.
	need += p - json;
	// plus a \0
	need++;
	// this should be enough
	if ( ! sb->reserve ( need ) ) return NULL;
	// for testing if we realloc
	char *mem = sb->getBufStart();

	// first is field
	//char *field = NULL;
	//long  fieldLen = 0;
	long  size;
	// ptr to current json item
	m_ji = NULL;
	// scan
	for ( ; *p ; p += size ) {
		// get size
		size = getUtf8CharSize ( p );

		// did we hit a '{'? that means the existing json item
		// is a parent of the item(s) inside the {}'s
		if ( *p == '{' ) {
			// push the current ji on the stack
			if ( m_ji ) m_stack[m_stackPtr++] = m_ji;
			// . this indicates the start of a json object
			// . addNewItem() will push the current item on stack
			m_ji = addNewItem();
			if ( ! m_ji ) return NULL;
			// current ji is an object type then
			m_ji->m_type = JT_OBJECT;
		}
		// pop the stack?
		if ( *p == '}' ) {
			// sanity
			if ( m_stackPtr <= 0 ) m_ji = NULL;
			// get it back
			else m_ji = m_stack[--m_stackPtr];
		}
		// array of things?
		if ( *p == '[' ) {
			// our parrent is array type then
			m_ji->m_type = JT_ARRAY;
		}

		// a quote?
		if ( *p == '\"' ) {
			// find end of quote
			char *end = p + 1;
			for ( ; *end ; end++ ) 
				if ( *end == '\"' && end[-1] != '\"' ) break;
			// field?
			char *x = end + 1;
			// skip spaces
			for ( ; *x && is_wspace_a(*x) ; x++ );
			// define the string
			char *str  = p + 1;
			long  slen = end - str;
			// . if a colon follows, it was a field
			if ( *x == ':' ) {
				// this will put the current item on the
				// stack if it is non-NULL
				m_ji = addNewItem();
				if ( ! m_ji ) return NULL;
				// set the item name referencing the orig json
				m_ji->m_name    = str;
				m_ji->m_nameLen = slen;
			}
			// . otherwise, it was field value, so index it
			// . TODO: later make field names compounded to
			//   better represent nesting?
			else {
				// if the current item is an array, make
				// one item for each string in array
				if ( m_ji->m_type == JT_ARRAY ) {
					// make a new one in safebuf
					m_ji = addNewItem();
				}
				// we are a string
				m_ji->m_type = JT_STRING;
				// get length decoded
				long curr = sb->length();
				// store decoded string right after jsonitem
				if ( ! sb->safeDecodeJSONToUtf8 ( str, slen,0))
					return NULL;
				// store length decoded json
				m_ji->m_valueLen = sb->length() - curr;
			}
			// skip over the string
			size = 1;
			p    = x;
			continue;
		}
		// if we hit a digit they might not be in quotes like
		// "crawled":123
		if ( is_digit ( *p ) ) {
			// find end of the number
			char *end = p + 1;
			for ( ; *end && is_digit(*p) ; end++ ) ;
			// define the string
			char *str  = p + 1;
			long  slen = end - str;
			// decode
			char c = str[slen];
			str[slen] = '\0';
			m_ji->m_valueLong = atol(str);
			m_ji->m_valueDouble = atof(str);
			str[slen] = c;
			m_ji->m_type = JT_NUMBER;
			// skip over the string
			size = 1;
			p    = end;
			continue;
		}
	}

	// for testing if we realloc
	char *memEnd = sb->getBufStart();
	if ( mem != memEnd ) { char *xx=NULL;*xx=0; }

	return (JsonItem *)sb->getBufStart();
}

	


void Json::test ( ) {

	char *json = "{\"tags\":[\"Apple Inc.\",\"Symbian\",\"IPad\",\"Music\"],\"summary\":\"Good timing and shrewd planning have played as much of a role as innovative thinking for the Silicon Valley juggernaut.\",\"icon\":\"http://www.onlinemba.com/wp-content/themes/onlinemba/assets/img/ico/apple-touch-icon.png\",\"text\":\"How did Apple rise through the ranks to become the world’s most profitable tech company? As it turns out, good timing and shrewd planning have played as much of a role as innovative thinking for the Silicon Valley juggernaut.For example, take the first MP3 player — MPMan, produced by South Korea-based SaeHan Information Systems. MPMan appeared in 1998, three years before the first iPods were released. As the original pioneer of portable MP3 player technology, SaeHan spent a good deal of time in court negotiating terms of use with various record companies. By 2001, a clear legal precedent was set for MP3 access — allowing Apple to focus less on courtroom proceedings and more on cutting-edge marketing campaigns for their new product."
		"When all else fails, they buy it: While iPads had fan boys salivating in the streets –the technology has been around for decades. One of the most obvious precursors to the iPad is FingerWorks, a finger gesture operated keyboard with a mouse very similar to Apple’s iPad controller. Fingerworks was bought in 2005 by none other than Apple – not surprisingly a couple years before the release of the iPhone and later the iPad.		 Of course, this isn’t to say that Apple doesn’t deserve to be the most valuable tech company in the world – just that innovation isn’t always about being first or best, sometimes, it’s just perception.\",\"stats\":{\"fetchTime\":2069,\"confidence\":\"0.780\"},\"type\":\"article\",\"meta\":{\"twitter\":{\"twitter:creator\":\"@germanny\",\"twitter:domain\":\"OnlineMBA.com\",\"twitter:card\":\"summary\",\"twitter:site\":\"@OnlineMBA_com\"},\"microdata\":{\"itemprop:image\":\"http://www.onlinemba.com/wp-content/uploads/2013/02/apple-innovates-featured-150x150.png\"},\"title\":\"3 Ways Apple Actually Innovates - OnlineMBA.com\",\"article:publisher\":\"https://www.facebook.com/OnlineMBAcom\",\"fb:app_id\":\"274667389269609\",\"og\":{\"og:type\":\"article\",\"og:title\":\"3 Ways Apple Actually Innovates - OnlineMBA.com\",\"og:description\":\"Good timing and shrewd planning have played as much of a role as innovative thinking for the Silicon Valley juggernaut.\",\"og:site_name\":\"OnlineMBA.com\",\"og:image\":\"http://www.onlinemba.com/wp-content/uploads/2013/02/apple-innovates-featured-150x150.png\",\"og:locale\":\"en_US\",\"og:url\":\"http://www.onlinemba.com/blog/3-ways-apple-innovates\"}},\"human_language\":\"en\",\"url\":\"http://www.onlinemba.com/blog/3-ways-apple-innovates\",\"title\":\"3 Ways Apple Actually Innovates\",\"textAnalysis\":{\"error\":\"Timeout during text analysis\"},\"html\":\"<div><div class=\\\"image_frame\\\"><img data-blend-adjustment=\\\"http://www.onlinemba.com/wp-content/themes/onlinemba/assets/img/backgrounds/bg.gif\\\" data-blend-mode=\\\"screen\\\" src=\\\"http://www.onlinemba.com/wp-content/uploads/2013/02/apple-innovates-invert-350x350.png\\\"></img></div><p>How did Apple rise"

		"\",\"supertags\":[{\"id\":856,\"positions\":[[7,12],[41,46],[663,668],[776,781],[1188,1193],[1380,1385],[1645,1650],[1841,1848],[2578,2583],[2856,2863],[2931,2936]],\"name\":\"Apple Inc.\",\"score\":0.8,\"contentMatch\":1,\"categories\":{\"1752615\":\"Home computer hardware companies\",\"27841529\":\"Technology companies of the United States\",\"33847259\":\"Publicly traded companies of the United States\",\"15168154\":\"Mobile phone manufacturers\",\"732736\":\"Retail companies of the United States\",\"9300270\":\"Apple Inc.\",\"23568549\":\"Companies based in Cupertino, "
		"California\",\"34056227\":\"Article Feedback 5\",\"37595560\":\"1976 establishments in California\",\"7415072\":\"Networking hardware companies\",\"699547\":\"Computer hardware companies\",\"37191508\":\"Software companies based in the San Francisco Bay Area\",\"855278\":\"Electronics companies\",\"5800057\":\"Steve Jobs\",\"7652766\":\"Display technology companies\",\"14698378\":\"Warrants issued in Hong Kong Stock Exchange\",\"4478067\":\"Portable audio player manufacturers\",\"31628257\":\"Multinational companies headquartered in the United States\",\"732825\":\"Electronics companies of the United States\",\"733759\":\"Computer companies of the United States\",\"6307421\":\"Companies established in 1976\"},\"type\":1,\"senseRank\":1,\"variety\":0.21886792452830184,\"depth\":0.6470588235294117},{\"id\":25686223,\"positions\":[[895,902],[2318,2325]],\"name\":\"Symbian\",\"score\":"
		"0.8,\"contentMatch\":0.9162303664921466,\"categories\":{\"33866248\":\"Nokia platforms\",\"20290726\":\"Microkernel-based operating systems\",\"39774425\":\"ARM operating systems\",\"2148723\":\"Real-time operating systems\",\"953043\":\"Smartphones\",\"10817505\":\"History of software\",\"17862682\":\"Mobile phone operating systems\",\"33569166\":\"Accenture\",\"2150815\":\"Embedded operating systems\",\"22533699\":\"Symbian OS\",\"22280474\":\"Mobile operating systems\"},\"type\":1,\"senseRank\":1,\"variety\":0.6566037735849057,\"depth\":0.6470588235294117},{\"id\":25970423,\"positions\":[[2639,2644],[2771,2775],[2864,2868]],\"name\":\"IPad\",\"score\":0.8,\"contentMatch\":1,\"categories\":{\"33578068\":\"Products introduced "
		"in 2010\",\"18083009\":\"Apple personal digital assistants\",\"23475157\":\"Touchscreen portable media players\",\"30107877\":\"IPad\",\"9301031\":\"Apple Inc. hardware\",\"27765345\":\"IOS (Apple)\",\"26588084\":\"Tablet computers\"},\"type\":1,\"senseRank\":1,\"variety\":0.49056603773584906,\"depth\":0.5882352941176471},{\"id\":18839,\"positions\":[[1945,1950],[2204,2209]],\"name\":\"Music\",\"score\":0.7,\"contentMatch\":1,\"categories\":{\"991222\":\"Performing arts\",\"693016\":\"Entertainment\",\"691484\":\"Music\"},\"type\":1,\"senseRank\":1,\"variety\":0.22264150943396221,\"depth\":0.7058823529411764}],\"media\":[{\"pixelHeight\":350,\"link\":\"http://www.onlinemba.com/wp-content/uploads/2013/02/apple-innovates-invert-350x350.png\",\"primary\":\"true\",\"pixelWidth\":350,\"type\":\"image\"}]}";


	SafeBuf sb;
	JsonItem *ji = parseJsonStringIntoJsonItems ( &sb , json );

	// print them out?
	log("json: type0=%li",(long)ji->m_type);

	return;
}
	
