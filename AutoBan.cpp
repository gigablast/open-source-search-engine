#include "gb-include.h"

#include "AutoBan.h"
#include <limits.h>
#include "SafeBuf.h"
#include "Pages.h"
#include "Loop.h"
#include "sort.h"
#include "Users.h"

AutoBan g_autoBan;
void resetHash(int fd, void *state);
static int32_t  *SorterTable;
//static ull_t *SorterTable2;


// #define EXPLICIT_ALLOW 0x8000000000000000ULL
// #define BANNED         0x4000000000000000ULL
// #define COUNT_OVERFLOW 0x0040040000000000ULL
// #define TOP_LONG_MASK  0x7fffffff00000000ULL
// #define DAY_COUNT_MASK 0x0000ffff00000000ULL


AutoBan::AutoBan() {
	m_detectKeys  = NULL;
	m_detectVals  = NULL;
	m_tableSize   = 0;
}

AutoBan::~AutoBan ( ) {
	reset();
}

void AutoBan::reset ( ) {
	if ( m_detectKeys ) {
		mfree ( m_detectKeys, m_tableSize * sizeof(int32_t), "AutoBanK" );
		m_detectKeys = NULL;
	}
	if ( m_detectVals ) {
		mfree ( m_detectVals, m_tableSize * sizeof(DetectVal),
			"AutoBanV" );
		m_detectVals = NULL;
	}
	m_ht.reset();
}

bool AutoBan::init() {
	m_tableSize = AUTOBAN_INITSIZE;
	m_numEntries = 0;

	m_detectKeys = (int32_t*)mmalloc(m_tableSize * sizeof(int32_t), 
				      "AutoBan");
	if(!m_detectKeys) {
		return false;
	}
	m_detectVals = (DetectVal*)mmalloc(m_tableSize * 
					   sizeof(DetectVal), 
					   "AutoBan");
	if(!m_detectVals) {
		mfree (m_detectKeys, m_tableSize * sizeof(int32_t), "AutoBan");
		return false;
	}
	memset(m_detectKeys, 0, sizeof(int32_t) * m_tableSize);

	//now add all banIps and allowIps from gconf.
	setCodesFromConf();
	if(!restore()) 
		log(LOG_WARN, "init: autoban could not restore dat file.");
	setFromConf();

  	g_loop.registerSleepCallback ( ONE_DAY , 
  				       NULL, 
  				       resetHash,0);
	
	return true;
}


bool AutoBan::addIp(int32_t ip, char action) {
	int32_t now = getTime();
	DetectVal d;
	d.m_minuteExpires = now + 60;
	d.m_dayExpires = now + ONE_DAY;
	d.m_dayCount = 0;
	d.m_minuteCount = 0;
	d.m_timesBanned = 0;
	d.m_flags = action | FROMCONF;
	bool retval = addKey(ip, &d);
	
	return retval;
}

bool AutoBan::addKey(int32_t ip, DetectVal* d) {
	int32_t tabsize = m_tableSize - 1;
	uint32_t i = (uint32_t)ip & tabsize;

	do {
		if(m_detectKeys[i] == ip) {
			m_detectKeys[i] = ip;
			m_detectVals[i].m_flags = d->m_flags;
			m_detectVals[i].m_minuteCount = d->m_minuteCount;
			m_detectVals[i].m_dayCount = d->m_dayCount;
			m_detectVals[i].m_minuteExpires = d->m_minuteExpires;
			m_detectVals[i].m_dayExpires = d->m_dayExpires;
			m_detectVals[i].m_timesBanned = d->m_timesBanned;
			break;
		}
		if(m_detectKeys[i] == 0) {
			if(m_numEntries * 1.2 > m_tableSize ) {
				//here we grow the table and adjust i to an 
				//empty slot in the new (bigger) table
				if(!growTable()) return false;
				int32_t tabsize = m_tableSize - 1;
				i = (uint32_t)ip & tabsize;
				while(m_detectKeys[i] != 0) 
					i = (i + 1) & tabsize;
			}
			m_detectKeys[i] = ip;
			m_detectVals[i].m_flags = d->m_flags;
			m_detectVals[i].m_minuteCount = d->m_minuteCount;
			m_detectVals[i].m_dayCount = d->m_dayCount;
			m_detectVals[i].m_minuteExpires = d->m_minuteExpires;
			m_detectVals[i].m_dayExpires = d->m_dayExpires;
			m_detectVals[i].m_timesBanned = d->m_timesBanned;
			m_numEntries++;
			break;
		}
		i = (i + 1) & tabsize;
	} while(1);
	return true;
}


void AutoBan::removeIp(int32_t ip) {
	int32_t tabsize = m_tableSize - 1;
	uint32_t i = (uint32_t)ip & tabsize;
	
	do {
		if(m_detectKeys[i] == ip) {
			m_detectKeys[i] = 0;
			i = (i + 1) & tabsize;
			while ( m_detectKeys[i] ) {
				int32_t key      = m_detectKeys[i];
				DetectVal *val = &m_detectVals[i];
				m_detectKeys[i] = 0;
				m_numEntries--;
				addKey(key, val);
				i = (i + 1) & tabsize;
			}

			m_numEntries--;
			break;
		}
		if(m_detectKeys[i] == 0) {
			break;
		}
		i = (i + 1) & tabsize;
	} while(1);
}


bool AutoBan::growTable() {
	int32_t                oldTableSize  = m_tableSize;
	int32_t               *oldDetectKeys = m_detectKeys;
	DetectVal          *oldDetectVals = m_detectVals;

	m_tableSize = m_tableSize << 1;

// 	log(LOG_INFO, "Autoban: Resize %"INT32" to %"INT32"", oldTableSize, 
// 	    m_tableSize);

	m_detectKeys = (int32_t*)mmalloc(m_tableSize * sizeof(int32_t), 
				      "AutoBanK");
	if(!m_detectKeys) {
		m_detectKeys = oldDetectKeys;
		m_detectVals = oldDetectVals;
		m_tableSize =  oldTableSize;
		return false;
	}
	m_detectVals = (DetectVal*)mmalloc(m_tableSize * 
						    sizeof(DetectVal),
						    "AutoBanB");
	if(!m_detectVals) {
		mfree (m_detectKeys, m_tableSize * sizeof(int32_t),
		       "AutoBan");
		m_detectKeys = oldDetectKeys;
		m_detectVals = oldDetectVals;
		m_tableSize =  oldTableSize;
		return false;
	}

	memset(m_detectKeys, 0, sizeof(int32_t) * m_tableSize);

	//now copy them to the new space.
	for(int32_t i = 0; i < oldTableSize; i++) {
		if(oldDetectKeys[i] == 0) continue;
		addKey(oldDetectKeys[i], &oldDetectVals[i]);
	}

	mfree(oldDetectKeys, oldTableSize * sizeof(int32_t),      "AutoBan");
	mfree(oldDetectVals, oldTableSize * sizeof(DetectVal), "AutoBan");

	return true;
}


void resetHash(int fd, void *state) {
	g_autoBan.cleanHouse();
}


//here we forget about people that haven't queried us in a while.
void AutoBan::cleanHouse() {
	int32_t now = getTime();
	for(int32_t i = 0; i < m_tableSize; i++) {
		if(m_detectKeys[i] == 0) continue;
		if(m_detectVals[i].m_flags & FROMCONF) continue;
		if(m_detectVals[i].m_timesBanned == 0 &&
		   m_detectVals[i].m_dayExpires < now)
			removeIp(m_detectKeys[i]);
	}
}


bool AutoBan::setCodesFromConf() {

	static bool s_firstTime = true;
	m_codeResetTime = getTime();
	char *p = g_conf.m_validCodes;
	while(*p) {
		if(!isspace(*p)) {
			int32_t len = 0;
			while(p[len] && !isspace(p[len])) len++;
			//now p points to a code, with length len.
			//log(LOG_WARN, "autoban code is %s %"INT32"", p, len);
			int32_t h = hash32(p,len);
			CodeVal cv;
			int32_t max = len;	if ( max > 30 ) max = 30;
			strncpy(cv.m_code,p,max);
			cv.m_code[max]='\0';
			cv.m_ip             = 0;
			cv.m_count          = 0;
			cv.m_bytesSent      = 0;
			cv.m_bytesRead      = 0;
			// we might be doing an update, so only set this
			// count to 0 the first time we are called on startup
			if ( s_firstTime )
				cv.m_outstanding = 0;
			cv.m_maxEver        = 0;
			cv.m_maxOutstanding = 5000;
			//m_numCodes++;
			p += len;
			// skip spaces or tabs
			while ( *p == ' ' || *p == '\t' ) p++;
			// do we got a number? that is the max outstanding cnt
			if ( is_digit ( *p ) )
				cv.m_maxOutstanding = atoi(p);
			// ensure no breach
			if ( cv.m_maxOutstanding < 10 ) 
				log("gb: client code %s has LOW max "
				    "outstanding limit of %"INT32"",
				    cv.m_code,cv.m_maxOutstanding);
			// skip the digits, until we hit \r or \n
			while ( is_digit ( *p ) ) p++;
			// now add it
			if ( ! m_ht.addKey ( h , cv ) ) return false;
		}
		p++;
	}
	s_firstTime = false;
	return true;
}


void AutoBan::setFromConf(){
	char* banIps = g_conf.m_banIps;
	char *start = banIps;
	do {
		while(*banIps && !isspace(*banIps)) banIps++; 
		int32_t ip = atoip(start, banIps - start);
		if(ip) {
			if(!addIp(ip, DENY)) {
				log(LOG_WARN, 
				   "autoban: malloc failed, couldn't add IP.");
			}
		}

		while(*banIps && isspace(*banIps)) banIps++; 
		start = banIps;
	} while(*banIps); 


	char* allowIps = g_conf.m_allowIps;
	start = allowIps;
	do {
		while(*allowIps && !isspace(*allowIps)) allowIps++; 
		int32_t ip = atoip(start, allowIps - start);
		if(ip) {
			if(!addIp(ip, ALLOW)) {
				log(LOG_WARN, 
				   "autoban: malloc failed, couldn't add IP.");
			}
		}
		while(*allowIps && isspace(*allowIps)) allowIps++; 
		start = allowIps;
	} while(*allowIps); 
}

bool AutoBan::hasCode(char *code, int32_t codeLen, int32_t ip ) {
	if(codeLen == 0) return false;
	int32_t h = hash32(code,codeLen);
	CodeVal *cv = m_ht.getValuePointer ( h );
	if ( ! cv ) 
		return log(LOG_INFO, "query: unrecognized code: %s", code);
	cv->m_ip = ip;
	cv->m_count++;
	return true;
}

// returns true if client is over the limit, false otherwise
bool AutoBan::incRequestCount ( int32_t ch , int32_t bytesRead ) {
	if ( ! ch ) return false;
	CodeVal *cv = m_ht.getValuePointer ( ch );
	if ( ! cv ) return false;
	// sanity check
	if ( bytesRead < 0 ) { char *xx=NULL;*xx=0; }
	// inc his count
	cv->m_outstanding++;
	cv->m_bytesRead += bytesRead;
	// the max ever?
	if ( cv->m_outstanding > cv->m_maxEver )
		cv->m_maxEver = cv->m_outstanding;
	// over limit?
	return ( cv->m_outstanding > cv->m_maxOutstanding );
}

void AutoBan::decRequestCount ( int32_t ch , int32_t bytesSent ) {
	if ( ! ch ) return;
	CodeVal *cv = m_ht.getValuePointer ( ch );
	if ( ! cv ) return;
	// sanity check
	if ( bytesSent < 0 ) { char *xx=NULL;*xx=0; }
	// dec the count
	cv->m_outstanding--;
	cv->m_bytesSent += bytesSent;
}

bool AutoBan::hasPerm(int32_t ip, 
		      char *code, int32_t codeLen, 
		      char *uip,  int32_t uipLen, 
		      TcpSocket   *s,
		      HttpRequest *r,
		      SafeBuf* testBuf,
		      bool justCheck ) {
	char *reqStr = r->getRequest();
	int32_t  reqLen  = r->getRequestLen();
	int32_t raw = r->getLong("xml", 0);
	int32_t isHuman = 0;
	if(code && hasCode(code, codeLen, ip )) {
		//don't close client's sockets
		if(s) s->m_prefLevel++;

		//no ip, but valid code, let them through.
		if(!uip) return true;
		ip = atoip(uip, uipLen);
		//	log(LOG_WARN, "has uip %s", uip);
		if(!ip) return true;
		//has code and uip, do the check.
		//the front end can administer a turing test
		//and tell us to unban them
		isHuman = r->getLong("ishuman", 0);
	}

	// if ip is local and uip is there, use it
	if ( uip && r->isLocal() ) {
		// it's local, let it through
		if( ! uip ) return true;
		// get the new ip then
		ip = atoip(uip, uipLen);
		//	log(LOG_WARN, "has uip %s", uip);
		if ( !ip ) return true;
		//has code and uip, do the check.
	}

	//now we check the ip block which the ip is in.
	uint32_t ipBlock = (uint32_t)ip & 0x0000ffff;
	uint32_t i = getSlot((uint32_t)ipBlock);
	if((uint32_t)m_detectKeys[i] == ipBlock) {
		if(m_detectVals[i].m_flags & ALLOW) {
			if ( justCheck ) return true;
			m_detectVals[i].m_dayCount++;
			if(s) s->m_prefLevel++;
			return true;
		}
		if(m_detectVals[i].m_flags & DENY) {
			if ( justCheck ) return false;
			m_detectVals[i].m_dayCount++;
			return false;
		}
	}

	//now we check the ip group which the ip is in.
	uint32_t ipGroup = (uint32_t)ip & 0x00ffffff;
	i = getSlot((uint32_t)ipGroup);
	if((uint32_t)m_detectKeys[i] == ipGroup) {
		if(m_detectVals[i].m_flags & ALLOW) {
			if ( justCheck ) return true;
			m_detectVals[i].m_dayCount++;
			if(s) s->m_prefLevel++;
			return true;
		}
		if(m_detectVals[i].m_flags & DENY) {
			if ( justCheck ) return false;
			m_detectVals[i].m_dayCount++;
			return false;
		}
	}


	i = getSlot((uint32_t)ip);
	int32_t now = getTime();

	int32_t banTest = r->getLong("bantest",0);
	if ( banTest ) {
		log("autoban: doing ban test");
		goto doTuringTest;
	}

	
	if(m_detectKeys[i] == ip) {
		if(m_detectVals[i].m_flags & ALLOW) {
			// do not inc if just checking, like for a gif file
			if ( justCheck ) return true;
			//explicitly allowed.
			//log(LOG_WARN,"autoban: %"INT32" allowed.", ip);
			m_detectVals[i].m_dayCount++;
			if(s) s->m_prefLevel++;
			return true;
		}
		if(m_detectVals[i].m_flags & DENY) {
			// do not inc if just checking, like for a gif file
			if ( justCheck ) return false;
			//banned by autoban, or explicity banned by matt.
			int32_t explicitBan = m_detectVals[i].m_flags & FROMCONF;
			//log(LOG_WARN,"autoban: %"INT32" rejected.", ip);
			if(!explicitBan &&
			   // MDW yippy project - no! don't unban bots!
			   //(m_detectVals[i].m_dayExpires < now || isHuman)) {
			   (isHuman)) {
				//they are unbanned for now, I guess.
				m_detectVals[i].m_flags &= ~DENY; 
				m_detectVals[i].m_dayExpires = now + ONE_DAY;
				m_detectVals[i].m_minuteExpires = now + 60;
				m_detectVals[i].m_dayCount = 1;
				m_detectVals[i].m_minuteCount = 1;
				log("autoban: auto-unbanning %s",iptoa(ip));
				//return true;
				goto checkSubstr;
			}

			m_detectVals[i].m_dayCount++;
			if(explicitBan) return false;
			
			if(uip) return false;
			goto doTuringTest;

		}

		// do not inc if just checking, like a gif file
		if ( justCheck ) return true;

		/*
		if( m_detectVals[i].m_minuteCount > 0 &&
		    // two requests in one second?
		    now == m_detectVals[i].m_minuteExpires - 60 ) {
			m_detectVals[i].m_flags |= DENY;
			log("autoban: second-banning %s",iptoa(ip));
			int32_t banUntil = now + 
				(ONE_DAY * 
				 (m_detectVals[i].m_timesBanned + 1));
			if(banUntil < 0 || 
			   m_detectVals[i].m_timesBanned == 255 ) {
				m_detectVals[i].m_dayExpires = 
					0x7fffffff;
			}
			else {
				m_detectVals[i].m_timesBanned++;
				m_detectVals[i].m_dayExpires =banUntil;
			}
			return false;
		}
		*/

		if(m_detectVals[i].m_minuteCount >= 
		   g_conf.m_numFreeQueriesPerMinute) {
			if(m_detectVals[i].m_minuteExpires > now) {
				//ban 'em, they are a cowbot, so they
				//don't get the turing test
				m_detectVals[i].m_flags |= DENY;
				log("autoban: minute-banning %s",iptoa(ip));
				int32_t banUntil = now + 
					(ONE_DAY * 
					 (m_detectVals[i].m_timesBanned + 1));
				if(banUntil < 0 || m_detectVals[i].m_timesBanned == 255 ) {
					m_detectVals[i].m_dayExpires = 0x7fffffff;
				}
				else {
					m_detectVals[i].m_timesBanned++;
					m_detectVals[i].m_dayExpires = banUntil;
				}
				return false;
				//goto doTuringTest;
			}
			else {
				m_detectVals[i].m_minuteExpires = now + 60;
				m_detectVals[i].m_minuteCount  = 0;
			}
		}
		if((uint32_t)m_detectVals[i].m_dayCount >= 
		   g_conf.m_numFreeQueriesPerDay) {
			if(m_detectVals[i].m_dayExpires > now) {
				//ban 'em
				log("autoban: day-banning %s",iptoa(ip));
				m_detectVals[i].m_flags |= DENY;
				if(m_detectVals[i].m_timesBanned != 255)
					m_detectVals[i].m_timesBanned++;
				m_detectVals[i].m_dayExpires = now + 
					(ONE_DAY * m_detectVals[i].
					 m_timesBanned);

				if(uip) return false;
				goto doTuringTest;
			}
			else {
				m_detectVals[i].m_dayExpires = now + ONE_DAY;
				m_detectVals[i].m_dayCount  = 0;
			}
		}
		m_detectVals[i].m_minuteCount++;
		m_detectVals[i].m_dayCount++;
		//return true;
		goto checkSubstr;
	}

	// do not inc if just checking, like for a gif file
	if ( justCheck ) return true;

	if(m_detectKeys[i] == 0) {
		if(m_numEntries * 1.2 > m_tableSize ) {
			//here we grow the table and adjust i to an 
			//empty slot in the new (bigger) table
			if(!growTable()) 
				//return true;
				goto checkSubstr;

			i = getSlot(ip);
		}
			
		
		m_detectKeys[i] = ip;
		m_detectVals[i].m_flags = 0;
		m_detectVals[i].m_minuteCount = 1;
		m_detectVals[i].m_dayCount    = 1;
		m_detectVals[i].m_minuteExpires = now + 60;
		m_detectVals[i].m_dayExpires = now + ONE_DAY;
		m_detectVals[i].m_timesBanned = 0;
		++m_numEntries;

		//log(LOG_WARN,"autoban: %"INT32" adding to empty slot.", 
		//ip);
		//return true;
		goto checkSubstr;
	}
	
	//we go here if someone is banned and they are trying to search
 doTuringTest:

	// sanity!
	if ( justCheck ) { char *xx=NULL;*xx=0; }

	if( raw == 0 ) {
		// did we get a good response from the turing test?
		if( g_turingTest.isHuman(r)) {
			m_detectVals[i].m_flags &= ~DENY; 
			//log("autoban: turing-unbanning %s",iptoa(ip));
			m_detectVals[i].m_dayExpires = now + ONE_DAY;
			m_detectVals[i].m_minuteExpires = now + 60;
			m_detectVals[i].m_dayCount = 1;
			m_detectVals[i].m_minuteCount = 1;
			log(LOG_INFO, "autoban: ip %s has unbanned "
			    "themselves", iptoa(ip));
			return true;
		}
		testBuf->safePrintf("<form method=get>");
		int32_t queryLen = 0;
		char* query = r->getValue("q" , &queryLen);
		int32_t start = r->getLong("s" , 0);
		if ( query )
			testBuf->safePrintf("<input type=hidden name=\"q\" "
					    "value=\"%s\">\n", query);
		if ( start > 0 )
			testBuf->safePrintf("<input type=hidden name=\"s\" "
					    "value=\"%"INT32"\">\n", start);
		int32_t gigabits = r->getLong("gigabits",0);
		if ( gigabits )
			testBuf->safePrintf("<input type=hidden name=gigabits "
					    "value=1>\n");

		//
		// yippy parms
		//
		char *ifs = r->getString("input-form",NULL);
		if ( ifs )
			testBuf->safePrintf("<input type=hidden "
					    "name=\"input-form\" "
					    "value=\"%s\">\n", ifs );
		char *vs = r->getString("v:sources",NULL);
		if ( vs )
			testBuf->safePrintf("<input type=hidden "
					    "name=\"v:sources\" "
					    "value=\"%s\">\n", vs );
		char *vp = r->getString("v:project",NULL);
		if ( vp )
			testBuf->safePrintf("<input type=hidden "
					    "name=\"v:project\" "
					    "value=\"%s\">\n", vp );
		char *qp = r->getString("query",NULL);
		if ( qp )
			testBuf->safePrintf("<input type=hidden "
					    "name=\"query\" "
					    "value=\"%s\">\n", qp);

		if ( banTest )
			testBuf->safePrintf("<input type=hidden "
					    "name=\"bantest\" "
					    "value=\"1\">\n");
			
		//
		// end yippy parms
		//

		// display the turing test so they can unban themselves
		g_turingTest.printTest(testBuf);
		testBuf->safePrintf("<br><center><input type=submit "
				    "value=\"submit\"></center><br>");
		testBuf->safePrintf("</form>");
	}
	return false;

checkSubstr:

	// sanity!
	if ( justCheck ) { char *xx=NULL;*xx=0; }

	// Look for regular expressions that may serve as a signature of 
	// a botnet attack

	char *banRegex = g_conf.m_banRegex;
	int32_t banRegexLen = g_conf.m_banRegexLen;
	if (!banRegex || !banRegexLen) return true;


	
	// Don't do regex...look for comma-separated lists of substrings
	int32_t start = 0;
	bool gotMatch = false;
	bool missedMatch = false;

	for (int32_t i=0;i<= banRegexLen;i++) {
		if (i != banRegexLen && 
		    banRegex[i] && banRegex[i] != '\n' && banRegex[i] != '\r'
		    && banRegex[i] != ',')
			continue;
		
		char c = banRegex[i];
		// NULL terminate
		banRegex[i] = '\0';
		// search for substr (must be longer than 2 chars
		if ( i - start > 2){
			if (strnstr2(reqStr, reqLen, &banRegex[start])) 
				gotMatch = true;
			else missedMatch = true;
		}
		banRegex[i] = c;
		start = i+1;
		// check the next substr if we're not at the 
		// end of line or end of buffer
		if (c != '\n' && c != '\r' && c != '\0') continue;
		
		// did we get all the substrings?
		if (gotMatch && !missedMatch) return false;
		// reset for the next set of substrings
		gotMatch = false;
		missedMatch = false;
	}
	
	return true;
}


// //just check, don't update the table.
// bool AutoBan::isBanned(uint32_t ip) {
// 	int32_t i = getSlot((uint32_t)ip);
// 	if(m_detectVals[i] & 0x4000000000000000ULL) {
// 		return true;
// 	}
// 	return false;
// }



int32_t AutoBan::getSlot(int32_t ip) {
	int32_t tabsize = m_tableSize - 1;
	uint32_t i = (uint32_t)ip & tabsize;
	do {
		if(m_detectKeys[i] == ip) {
			return i;
		}
 		if(m_detectKeys[i] == 0) {
			return i;
 		}
		i = (i + 1) & tabsize;
	} while(1);
	return i;
}


//find repeated spaces and trim them down to only 
//one space in a row.
void trimWhite(char* beginning) {
	char *to = beginning;
	char *from;
	bool lastIsSpace = false;
	
	while(*to != '\0') {
		if(isspace(*to)) {
			if(lastIsSpace) {
				from = to;
				char* begin = to + 1;
				while(isspace(*from)) from++;
				while(*to) *to++ = *from++;
				trimWhite(begin);
				return;
			}
			else lastIsSpace = true;
			if(*to == '\r') *to = '\n';
		}
		else lastIsSpace = false;
		to++;
	}
}


//same as strstr, but this makes sure that you are on a word
//boundary.
char* findToken(char* body, char* substr, int32_t substrLen) {
	char *start = body;
	while(body) {
		body = strstr(body, substr);
		if(body) {
			//check the body and the end to make
			//sure that what we have is not a substring of 
			//a larger string
			if((body == start || 
			    isspace(*(body - 1)))
			   &&
			   (isspace(*(body + substrLen)) || 
			    *(body + substrLen) == '\0')) {
				break;
			}
			else body += substrLen;
		}
		else break;
	}
	return body;
}


static int ip_cmp ( const void *h1 , const void *h2 ) {
        char* tmp1; 
	char* tmp2;
        tmp1 = ((char *)&(SorterTable[*(int32_t*)h1]));
	tmp2 = ((char *)&(SorterTable[*(int32_t*)h2]));
	return strncmp(tmp1, tmp2, 4);
}



#define BABY_BLUE  "e0e0d0"
#define LIGHT_BLUE "d0d0e0"
#define DARK_BLUE  "c0c0f0"
#define GREEN      "00ff00"
#define RED        "ff0000"
#define YELLOW     "ffff00"


bool sendPageAutoban ( TcpSocket *s , HttpRequest *r ) {
	return g_autoBan.printTable(s,r);
}

bool AutoBan::printTable( TcpSocket *s , HttpRequest *r ) {
	SafeBuf sb(512 * 512,"autobbuf");
	//read in all of the possible cgi parms off the bat:
	//int32_t  user     = g_pages.getUserType( s , r );
	//char *username = g_users.getUsername(r);
	//char *pwd  = r->getString ("pwd");

	char *coll = r->getString ("c");

	int32_t banIpsLen;
	char *banIps = r->getString ("banIps" , &banIpsLen , NULL);

	int32_t allowIpsLen;
	char *allowIps = r->getString ("allowIps" , &allowIpsLen , NULL);

 	int32_t clearLen;
 	char *clear = r->getString ("clear" , &clearLen , NULL);

	bool changed = false;

 	int32_t validCodesLen;
 	char *validCodes = r->getString ("validCodes", &validCodesLen, NULL);

	int32_t showAllIps = r->getLong("showAllIps", 0);
	int32_t showLongView = r->getLong("int32_tview", 0);

	// do it all from parm now
	//int32_t banRegexLen;
	//char *banRegex = r->getString("banRegex", &banRegexLen, NULL);
	

// 	char *ss = sb.getBuf();
// 	char *ssend = sb.getBufEnd();
	g_pages.printAdminTop ( &sb, s , r );

	//sb.incrementLength(sss - ss);

	// MDW: moved to here

	int32_t now = getTime();
	
	int32_t days;
	int32_t hours;
	int32_t minutes;
	int32_t secs;
	int32_t msecs;

	if(r->getLong("resetcodes", 0)) {
		setCodesFromConf();
	}

	sb.safePrintf("\n<br><br><table %s>\n",TABLE_STYLE);

	getCalendarFromMs((now - m_codeResetTime) * 1000,
			  &days, 
			  &hours, 
			  &minutes, 
			  &secs,
			  &msecs);
	sb.safePrintf("<tr><td colspan=18 bgcolor=#%s>"
		      "<center><b>Code Usage "
		      "(<a href=\"/admin/"
		      "autoban?c=%s&resetcodes=1\">reset</a> "
		      "%"INT32" days %"INT32" hours %"INT32" "
		      "minutes %"INT32" sec ago)"
		      "</b></center></td></tr>", 
		      DARK_BLUE,
		      coll,
		      days, 
		      hours, 
		      minutes, 
		      secs);
	sb.safePrintf("<tr bgcolor=#%s>"
		      "<td><center><b>Code</b></center></td>"
		      "<td><center><b>IP</b></center></td>"
		      "<td><center><b>Query Count</b></center></td>"

		      "<td><center><b>Bytes Read</b></center></td>"
		      "<td><center><b>Bytes Sent</b></center></td>"
		      
		      "<td><center><b>Outstanding Count</b></center></td>"
		      "<td><center><b>Most Ever Outstanding</b></center></td>"
		      "<td><center><b>Max Outstanding</b></center></td>"
		      "</tr>", 
		      LIGHT_BLUE);


	for(int32_t i = 0; i < m_ht.getNumSlots(); i++) {
		if ( m_ht.getKey ( i ) == 0 ) continue;
		CodeVal *cv = m_ht.getValuePointerFromSlot ( i );
		if ( ! cv ) continue;
		
		sb.safePrintf("<tr>");
		sb.safePrintf("<td>");
		sb.copyToken(cv->m_code);//m_codeVals[i].m_code);
		sb.safePrintf("</td>");
		sb.safePrintf("<td><center>%s</center> </td>",
			      iptoa(cv->m_ip));
		sb.safePrintf("<td><center>%"INT64"</center></td>", 
			      cv->m_count);

		sb.safePrintf("<td><center>%"INT64"</center></td>", 
			      cv->m_bytesRead);
		sb.safePrintf("<td><center>%"INT64"</center></td>", 
			      cv->m_bytesSent);

		sb.safePrintf("<td><center>%"INT32"</center></td>", 
			      cv->m_outstanding);
		sb.safePrintf("<td><center>%"INT32"</center></td>", 
			      cv->m_maxEver);
		if ( cv->m_maxOutstanding != 50 )
			sb.safePrintf("<td><center><b>%"INT32"</b></center></td>", 
				      cv->m_maxOutstanding);
		else
			sb.safePrintf("<td><center>%"INT32"</center></td>", 
				      cv->m_maxOutstanding);

		sb.safePrintf("</tr>");
		
	}
	sb.safePrintf ("</table><br><br>\n" );


 	if(clear && clearLen < 64) {
 		int32_t ip = atoip(clear, clearLen);
 		if(ip) {
			removeIp(ip);
			char *beginning;
			char ipbuf[64];//gotta NULL terminate for strstr
			gbmemcpy(ipbuf, clear, clearLen);
			ipbuf[clearLen] = '\0';
			beginning = findToken(g_conf.m_banIps, ipbuf, 
					      clearLen);
			if(beginning) {
				char *to = beginning;
				char *from = beginning + clearLen;
				while(*to) *to++ = *from++;
			}
			beginning = findToken(g_conf.m_allowIps, ipbuf,
					      clearLen);
			if(beginning) {
				char *to = beginning;
				char *from = beginning + clearLen;
				while(*to) *to++ = *from++;
			}
			changed = true;
 		}
 	}

 	int32_t allowLen;
 	char *allow = r->getString ( "allow" , &allowLen , NULL );
 	if(allow && allowLen < 64) {
 		int32_t ip = atoip(allow, allowLen);
		
 		if(ip) {
			char *beginning;
			char ipbuf[64];//gotta NULL terminate for strstr
			gbmemcpy(ipbuf, allow, allowLen);
			ipbuf[allowLen] = '\0';
			beginning = findToken(g_conf.m_allowIps, ipbuf, 
					      allowLen);
			if(!beginning) {
				//its not present, so add it.
				char *p = g_conf.m_allowIps;
				while(*p) p++;
				if(p - g_conf.m_allowIps + allowLen + 2 
				   < AUTOBAN_TEXT_SIZE) {
					*p++ = '\n';
					gbmemcpy(p, ipbuf,allowLen);
					*(p + allowLen) = '\0';
				}
				else {
					sb.safePrintf("<font color=red>"
						      "Not enough stack space "
						      "to fit allowIps.  "
						      "Increase "
						      "AUTOBAN_TEXT_SIZE in "
						      "Conf.h. "
						      "Had %"INT32" need %"INT32"."
						      "</font>", 
						      (int32_t)AUTOBAN_TEXT_SIZE,
						      (int32_t)(p - g_conf.m_allowIps + 
								allowLen + 2));
					goto dontRemove1;
				}
			}
			beginning = findToken(g_conf.m_banIps, ipbuf, 
					      allowLen);
			if(beginning) {
				//remove it from banned if present.
				char *to = beginning;
				char *from = beginning + allowLen;
				while(*to) *to++ = *from++;
			}

			changed = true;
 		}
 	}
 dontRemove1:
 	int32_t denyLen;
 	char *deny = r->getString ( "deny" , &denyLen , NULL );
 	if(deny && denyLen < 64) {
 		int32_t ip = atoip(deny, denyLen);
		
 		if(ip) {
			char *beginning;
			char ipbuf[64];//gotta NULL terminate for strstr
			gbmemcpy(ipbuf, deny, denyLen);
			ipbuf[denyLen] = '\0';
			beginning = findToken(g_conf.m_banIps, ipbuf, denyLen);
			if(!beginning) {
				//its not present, so add it.
				char *p =g_conf.m_banIps;
				while(*p) p++;
				if(p - g_conf.m_banIps + denyLen + 2 < 
				   AUTOBAN_TEXT_SIZE) {
					*p++ = '\n';
					gbmemcpy(p, ipbuf,denyLen);
					*(p + denyLen) = '\0';
				}
				else {
					sb.safePrintf("<font color=red>Not "
						      "enough stack space "
						      "to fit bannedIPs.  "
						      "Increase "
						      "AUTOBAN_TEXT_SIZE in "
						      "Conf.h. "
						      "Had %i need %"INT32"."
						      "</font>", 
						      AUTOBAN_TEXT_SIZE,
						      (int32_t)(p - g_conf.m_banIps +
								denyLen + 2));
					goto dontRemove2;
				}
			}
			beginning = findToken(g_conf.m_allowIps, ipbuf,
					      denyLen);
			if(beginning) {
				//remove it from allowed list if present.
				char *to = beginning;
				char *from = beginning + denyLen;
				while(*to) *to++ = *from++;
			}
			changed = true;
 		}
 	}
 dontRemove2:

	if(!g_conf.m_doAutoBan) {
		sb.safePrintf("<center><font color=red><b>Autoban is disabled, "
			      "turn it on in Master Controls.</b></font></center><br>");
	}

 	if(validCodes) {
		if(validCodesLen >= AUTOBAN_TEXT_SIZE) {
			sb.safePrintf("<font color=red>Not enough stack space "
				      "to fit codes.  "
				      "Increase AUTOBAN_TEXT_SIZE in Conf.h. "
				      "Had %i need %"INT32".</font>", 
				      AUTOBAN_TEXT_SIZE,
				      validCodesLen);
			validCodes = NULL;
			validCodesLen = 0;
		}
		else {
			gbmemcpy(g_conf.m_validCodes, validCodes, validCodesLen);
			g_conf.m_validCodes[validCodesLen] = '\0';
			trimWhite(g_conf.m_validCodes);
			setCodesFromConf();
		}
	}



	//first remove all of the ips in the conf, then add the passed in 
	//  ones to the conf parm; 
	if (banIps) {
		//ack, the browser puts in crlf when this comes back, so
		//we will have a longer string here than the one we sent 
		//out. trim back all extrainious whitespace before we do
		//bounds checking.
		trimWhite(banIps);
		banIpsLen = gbstrlen(banIps);
		if(banIpsLen >= AUTOBAN_TEXT_SIZE) {
			sb.safePrintf("<font color=red>Not enough stack space "
				      "to fit bannedIps.  "
				      "Increase AUTOBAN_TEXT_SIZE in Conf.h. "
				      "Had %i need %"INT32".</font>", 
				      AUTOBAN_TEXT_SIZE,
				      banIpsLen);
			banIpsLen = AUTOBAN_TEXT_SIZE - 1;
		}
		for(int32_t i = 0; i < m_tableSize; i++) {
			if(m_detectKeys[i] == 0) continue;
			//check the 'set from conf' bit, and clear those.
			if(m_detectVals[i].m_flags & FROMCONF) {
				removeIp(m_detectKeys[i]);
			}
		}
		gbmemcpy(g_conf.m_banIps, banIps, banIpsLen);
		g_conf.m_banIps[banIpsLen] = '\0';
		changed = true;
	}
	if (allowIps) {
		trimWhite(allowIps);
		allowIpsLen = gbstrlen(allowIps);

		if(allowIpsLen >= AUTOBAN_TEXT_SIZE) {
			sb.safePrintf("<font color=red>Not enough stack space "
				      "to fit allowIps.  "
				      "Increase AUTOBAN_TEXT_SIZE in Conf.h. "
				      "Had %i need %"INT32".</font>", 
				      AUTOBAN_TEXT_SIZE,
				      allowIpsLen);
			allowIpsLen = AUTOBAN_TEXT_SIZE - 1;
		}
		for(int32_t i = 0; i < m_tableSize; i++) {
			if(m_detectKeys[i] == 0) continue;
			//check the 'set from conf' bit, and clear those.
			if(m_detectVals[i].m_flags & FROMCONF) {
				removeIp(m_detectKeys[i]);
			}
		}
		gbmemcpy(g_conf.m_allowIps, allowIps, allowIpsLen);
		g_conf.m_allowIps[allowIpsLen] = '\0';
		changed = true;
	}
	if(changed) {
		trimWhite(g_conf.m_allowIps);
		trimWhite(g_conf.m_banIps);
		setFromConf();
	}



	sb.safePrintf("\n<table %s>\n",TABLE_STYLE);
	sb.safePrintf("<tr><td colspan=2 bgcolor=#%s>"
		      "<center><b>Add IPs</b></center></td></tr>", 
		      DARK_BLUE);

// 	ss = sb.getBuf();
// 	ssend = sb.getBufEnd();
	g_parms.printParms (&sb, s, r);
	//	sb.incrementLength(sss - ss);



	sb.safePrintf ("<tr><td>"
		       "<center>" 
		       "<input type=submit value=\"Update\" "
		       "method=\"POST\" border=0>"
		       "</center></td></tr>");

	sb.safePrintf ("</table><br><br>\n" );



	if(!showLongView) {
		sb.safePrintf("<b><a href=\"autoban"
			      "?c=%s"
			      "&showAllIps=%"INT32""
			      "&int32_tview=1\">Show watched ips table...</a></b>",
			      coll,
			      showAllIps);
		return g_httpServer.sendDynamicPage ( s , 
						      sb.getBufStart() , 
						      sb.length() , 
						      -1 , 
						      false);
	}

	/////////////////////////////////////////////////////////////////////

	sb.safePrintf("\n<table %s>\n",TABLE_STYLE);

	sb.safePrintf("<tr><td colspan=3 bgcolor=#%s>"
		      "<center><b>Watched Ips</b></center></td></tr>", 
		      DARK_BLUE);

	sb.safePrintf("<tr bgcolor=#%s>"
		      "<td><center><b>IP</b></center></td>"
		      "<td><center><b>Description</b></center></td>"
		      //		      "<td><center><b>Time Added</b></center></td>"
		      "<td><center><b>Allow/Deny/Clear</b></center></td>"
		      "</tr>", 
		      LIGHT_BLUE);




	int32_t *sortedIndices = (int32_t*)mmalloc(m_tableSize * sizeof(int32_t), 
					     "AutoBanH");

	if(!sortedIndices) {
		return g_httpServer.sendErrorReply(s,500,mstrerror(ENOMEM));
	}

	int32_t numEntries = 0;
	for(int32_t i = 0; i < m_tableSize; i++) {
		if(m_detectKeys[i] == 0) continue;
		sortedIndices[numEntries++] = i;
	}
	SorterTable = m_detectKeys;

        gbsort(sortedIndices, numEntries, sizeof(int32_t), ip_cmp);


	//lets put each class of watched ip in its own safebuf then cat 
	//them together at the end.
	
	SafeBuf allowed;
	SafeBuf banned; 
	SafeBuf feedLeachers; 
	SafeBuf cowBots; 
	SafeBuf *e;

	for(int32_t j = 0; j < numEntries; j++) {
		int32_t i = sortedIndices[j];
		if(m_detectKeys[i] == 0) continue;
		//if(!(m_detectVals[i].m_flags & FROMCONF)) continue;
		bool allow =  m_detectVals[i].m_flags & ALLOW && 
			m_detectVals[i].m_flags & FROMCONF;
		bool deny  =  m_detectVals[i].m_flags & DENY && 
			m_detectVals[i].m_flags & FROMCONF;
		bool explicitban = deny && m_detectVals[i].m_flags & FROMCONF;
		uint16_t dayCount = m_detectVals[i].m_dayCount;
		unsigned char minuteCount = m_detectVals[i].m_minuteCount;

		bool day =    dayCount >= g_conf.m_numFreeQueriesPerDay;
		bool minute = minuteCount >= g_conf.m_numFreeQueriesPerMinute;

		char *description;
		char *color;

		if(allow) {
			color = GREEN;
			description = "Allowed";
			e = &allowed;
		} 
		else if(explicitban) {
			color = RED;
			description = "Banned";
			e = &banned;
		}
		else if(minute) {
			color = RED;
			description = "Cow Bot";
			e = &cowBots;
		}
		else if(day) {
			color = RED;
			description = "Feed Leacher";
			e = &feedLeachers;
		}
		else {
			//this can happen when someone was banned due to 
			//exceeding the quota, then the quota was lowered.
			
			m_detectVals[i].m_flags &= ~DENY;
			//log("autoban: ohshit-banning %s",iptoa(s->m_ip));
			continue;
		}

		
		e->safePrintf("<tr>");

		e->safePrintf("<td bgcolor=#%s><center>%s</center></td><td>"
			      "<center>%s</center></td>"

// 			      "<td><center>"
// 			      "%"INT32" days %"INT32" hrs %"INT32" min ago"
// 			      "</center></td>"

			      "<td><center><a href=\"/admin/"
			      "autoban?c=%s&allow=%s&showAllIps=%"INT32"\">" 
			      "allow/</a>"

			      "<a href=\"/admin/"
			      "autoban?c=%s&deny=%s&showAllIps=%"INT32"\">" 
			      "deny/</a>"

			      "<a href=\"/admin/"
			      "autoban?c=%s&clear=%s&showAllIps=%"INT32"\">"
			      "clear</a></center>"
			      "</td>",color, 
			      iptoa(m_detectKeys[i]),
			      description,

			      //      days,hours,minutes,

			      coll,
			      iptoa(m_detectKeys[i]),
			      showAllIps,
			      coll,
			      iptoa(m_detectKeys[i]),
			      showAllIps,
			      coll,
			      iptoa(m_detectKeys[i]),
			      showAllIps);
		e->safePrintf("</tr>");
	}

	sb.cat(allowed);
	sb.cat(banned); 
	sb.cat(feedLeachers); 
	sb.cat(cowBots); 

	sb.safePrintf ("</table><br><br>\n" );


	// MDW moved from here

	sb.safePrintf("\n<br><br><table %s>\n",TABLE_STYLE);

	sb.safePrintf("<tr><td colspan=5 bgcolor=#%s>"
		      "<center><b>Control Panel</b></center></td></tr>", 
		      DARK_BLUE);

	sb.safePrintf("<tr>"
		      "<td bgcolor=#%s><center><b>Show Ips by Number of Queries"
		      "</b></center></td>",
		      LIGHT_BLUE);
	sb.safePrintf("<td><center><font color=red><b><a href=\"/admin/"
		      "autoban?c=%s&showAllIps=0\">"
		      "0 Queries</a></b>"
		      "</font></center></td>",
		      coll);
	sb.safePrintf("<td><center><font color=red><b><a href=\"/admin/"
		      "autoban?c=%s&showAllIps=1\">"
		      "1 Query</a></b>"
		      "</font></center></td>",
		      coll);
	sb.safePrintf("<td><center><font color=red><b><a href=\"/admin/"
		      "autoban?c=%s&showAllIps=10\">"
		      "10 Queries</a></b>"
		      "</font></center></td>",
		      coll);
	sb.safePrintf("<td><center><font color=red><b><a href=\"/admin/"
		      "autoban?c=%s&showAllIps=100\">"
		      "100 Queries</a></b>"
		      "</font></center></td></tr>",
		      coll);

	sb.safePrintf ("</table><br><br>\n");



	if(!showAllIps) {

		char* ss = (char*) sb.getBufStart();
		int32_t sslen = sb.length();
		mfree(sortedIndices, m_tableSize * sizeof(int32_t),"AutoBanH");

		return g_httpServer.sendDynamicPage ( s , ss , sslen , -1 , false);
	}
	

	sb.safePrintf("\n<br><br><table %s>\n",TABLE_STYLE);

	sb.safePrintf("<tr><td colspan=6 bgcolor=#%s>"
		      "<center><b>Queries Today</b></center></td></tr>", 
		      DARK_BLUE);

	sb.safePrintf("<tr bgcolor=#%s>"
		      "<td><center><b>IP</b></center></td>"
		      "<td><center><b>Minute count</b></center></td>"
		      "<td><center><b>Day count</b></center></td>"
		      "<td><center><b>Time Until Reset</b></center></td>"
		      "<td><center><b>Times Banned</b></center></td>"
		      "<td><center><b>Allow/Deny</b></center></td>"
		      "</tr>", 
		      LIGHT_BLUE);


	char minBuf[128];
	char dayBuf[128];
	uint32_t lastIpGroup = 0;
	for(int32_t j = 0; j < numEntries; j++) {
		int32_t i = sortedIndices[j];
		int32_t  dayCount = m_detectVals[i].m_dayCount;
		unsigned char minuteCount = m_detectVals[i].m_minuteCount;

		if(!(m_detectVals[i].m_flags & FROMCONF)) {
			if(m_detectVals[i].m_minuteExpires < now) 
				minuteCount = 0;
			if(!(m_detectVals[i].m_flags & DENY) && 
			   m_detectVals[i].m_dayExpires < now) 
				dayCount = 0;
		}
		//a hack:
		if( dayCount < showAllIps) continue;

		char *color = YELLOW;
		
		if(m_detectVals[i].m_flags & ALLOW) {
			color = GREEN;
			snprintf(minBuf, 128, "--");
			snprintf(dayBuf, 128, "%"INT32"", dayCount);
		}
		else if(m_detectVals[i].m_flags & DENY) {
			color = RED;
			snprintf(minBuf, 128, "--");
			snprintf(dayBuf, 128, "%"INT32"", dayCount);
		} 
		else {
			snprintf(minBuf, 128, "%"INT32"", (int32_t)minuteCount);
			snprintf(dayBuf, 128, "%"INT32"", (int32_t)dayCount);
		}

		uint32_t thisIpGroup = (uint32_t)m_detectKeys[i] & 
			0x00ffffff;

		sb.safePrintf("<tr><center>");

		if(m_detectVals[i].m_flags & FROMCONF) {
			sb.safePrintf("<td bgcolor=#%s><center>%s%s%s</center></td>"
				      "<td><center>%s</center> </td>"
				      "<td><center>%s</center></td>" 
				      "<td><center><font color=red>"
				      "<b>NEVER</b>"
				      "</font></center></td>"
				      "<td><center>--</center></td>",
				      color, 
				      (thisIpGroup == lastIpGroup)?"<b>":"",
				      iptoa(m_detectKeys[i]),
				      (thisIpGroup == lastIpGroup)?"</b>":"",
				      minBuf,
				      dayBuf);
		}
		else {
			//they haven't done a query since being unbanned,
			//unban them now so we don't get negative resets displayed.
			/*
			  no, don't unban the bots!!! MDW yippy project
			if(m_detectVals[i].m_dayExpires < now) {
				m_detectVals[i].m_flags &= ~DENY; 
				//log("autoban: dayexpire-unbanning %s",
				//    iptoa(ip));
				m_detectVals[i].m_dayExpires = now + ONE_DAY;
				m_detectVals[i].m_minuteExpires = now + 60;
				m_detectVals[i].m_dayCount = 0;
				m_detectVals[i].m_minuteCount = 0;
				sb.safePrintf("</center></tr>");
				continue;
			}
			*/

			getCalendarFromMs((m_detectVals[i].m_dayExpires - now)* 1000,
					  &days, 
					  &hours, 
					  &minutes, 
					  &secs,
					  &msecs);

			sb.safePrintf("<td bgcolor=#%s><center>%s%s%s</center></td>"
				      "<td><center>%s</center> </td>"
				      "<td><center>%s</center></td>" 
				      "<td><center><font color=red>"
				      "<b>%"INT32" days %"INT32" hrs %"INT32" min %"INT32" sec</b>"
				      "</font></center></td>"
				      "<td><center>%i</center></td>",
				      color, 
				      (thisIpGroup == lastIpGroup)?"<b>":"",
				      iptoa(m_detectKeys[i]),
				      (thisIpGroup == lastIpGroup)?"</b>":"",
				      minBuf,
				      dayBuf,
				      days, hours, minutes, secs,
				      m_detectVals[i].m_timesBanned);
		}
		sb.safePrintf("<td><center>"
			      "<a href=\"/admin/"
			      "autoban?c=%s&allow=%s&showAllIps=%"INT32"\">" 
			      "allow/</a>"
			      "<a href=\"/admin/"
			      "autoban?c=%s&deny=%s&showAllIps=%"INT32"\">" 
			      "deny</a></center>"
			      "</td>",
			      coll,
			      iptoa(m_detectKeys[i]),
			      showAllIps,
			      coll,
			      iptoa(m_detectKeys[i]),
			      showAllIps);

		sb.safePrintf("</center></tr>");
		lastIpGroup = thisIpGroup;
	}


	sb.safePrintf ("</table><br><br>\n" );


	char* ss = (char*) sb.getBufStart();
	int32_t sslen = sb.length();

	mfree(sortedIndices, m_tableSize * sizeof(int32_t),"AutoBanH");

	return g_httpServer.sendDynamicPage ( s , ss , sslen , -1 , false);
}


bool AutoBan::save() {
	char tmp[512 * 512];
	SafeBuf p(tmp, 512 * 512);
	p += m_numEntries;
	for(int32_t i = 0; i < m_tableSize; i++) {
		if(m_detectKeys[i] == 0) continue;
		p += m_detectKeys[i];
		p.safeMemcpy((char*)&m_detectVals[i], sizeof(m_detectVals[i]));
	}
	return p.dumpToFile("autoban-saved.dat");
}


bool AutoBan::restore() {
	char tmp[512 * 512];
	SafeBuf p(tmp, 512 * 512);
	if(p.fillFromFile("autoban-saved.dat")<0)
		return false;
	if (p.length() <= 0 ) return true;
	char* buf = (char*) p.getBufStart();
	char* bufEnd = (char*) p.getBufEnd();
	// catLoop:
	int32_t numEntries = *(int32_t*)buf;
	buf += sizeof(int32_t);
	for(int32_t i = 0; i < numEntries; i++) {
		if ( buf + 4 > bufEnd ) break;
		int32_t ip = *(int32_t*)buf;
		buf += sizeof(int32_t);
		addKey(ip, (DetectVal*)buf);
		buf += sizeof(DetectVal);
		if(buf > bufEnd) return false;
	}
	log("autoban: read %"INT32" entries",numEntries);
	// more to read? return no if not
	// this was  a hack when catting two autoban-saved.dat files together
	//if ( buf + 4 < bufEnd && numEntries )
	//	goto catLoop;
	// all done
	return true;
}

