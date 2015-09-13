#include "gb-include.h"

#include "Collectiondb.h"
#include "Pages.h" 
#include "SafeBuf.h" 
#include "Msg1f.h"
#include "Parms.h"
#include "Users.h"
#define  MAX_LOG_WINDOW 8192

static void gotRemoteLogWrapper(void *state, UdpSlot *slot);

struct StateLogView {
	TcpSocket *m_s;
	SafeBuf    m_sb;
	int32_t       m_numOutstanding;
	//	char       m_readBuf[MAX_HOSTS * MAX_LOG_WINDOW];
	//we need to malloc this now, incase they want to see more of the log.
	int32_t       m_readBufSize;
	char      *m_readBuf;
	char      *m_readBufPtrs[MAX_HOSTS];
	bool       m_filters[8];
	char      *m_filterStr[8];
	char       m_numFilts;
	char      *m_lastPtr;
	int32_t       m_numSlots;
};

static char *s_magicStr = "4j3.8x*";
#define BABY_BLUE  "e0e0d0"
//#define LIGHT_BLUE "d0d0e0"
//#define DARK_BLUE  "c0c0f0"

bool sendPageLogView    ( TcpSocket *s , HttpRequest *r ) {


	StateLogView* st;
	try { st = new (StateLogView); }
	catch ( ... ) {
		g_errno = ENOMEM;
		return g_httpServer.sendErrorReply(s,500,"Out of memory");
	}
	mnew ( st , sizeof(StateLogView) , "StateLogViewA" );
	st->m_numOutstanding = 0;
	st->m_numSlots = 0;
	st->m_s = s;
	
	SafeBuf *p = &st->m_sb;
	p->reserve2x(65535);
	
 	//int32_t  user     = g_pages.getUserType( s , r );
 	//char *username = g_users.getUsername(r);
	//char *pwd  = r->getString ("pwd");
 	//char *coll = r->getString ("c");
	int32_t refreshRate = r->getLong("rr", 0);
	int32_t sampleSize  =  r->getLong("ss", 2048);
	if(refreshRate > 0) 
		p->safePrintf("<META HTTP-EQUIV=\"refresh\" "
			      "content=\"%"INT32"\"\\>", 
			      refreshRate);

	// 	char *ss = p->getBuf();
	// 	char *ssend = p->getBufEnd();
	g_pages.printAdminTop ( p, s, r );

	//	p->incrementLength(sss - ss);

	int32_t nh = g_hostdb.getNumHosts();
	bool blocked = true;

	p->safePrintf("<SCRIPT LANGUAGE=\"javascript\">"
		     "function checkAll(form, name, num) "
		      "{ "
		      "    for (var i = 0; i < num; i++) {"
		      "      var e = document.getElementById(name + i);"
		      "      e.checked = !e.checked ; "
		      "}"
		      "} "
		      "</SCRIPT> ");
	p->safePrintf("<form name=\"fo\">");

	p->safePrintf("\n<table %s>\n",TABLE_STYLE);
	p->safePrintf("<tr class=hdrow><td colspan=2>"
		      "<center><b>Log View</b></center>"
		      "</td></tr>");
	
	p->safePrintf("<tr bgcolor=%s>"
		      "<td>Refresh Rate:</td><td><input type=\"text\""
		      " name=\"rr\" value=\"%"INT32"\" size=\"4\"></td></tr>", 
		      LIGHT_BLUE,
		      refreshRate);

	p->safePrintf("<tr bgcolor=%s>"
		      "<td>Sample Size:</td><td><input type=\"text\""
		      " name=\"ss\" value=\"%"INT32"\" size=\"4\">",
		      LIGHT_BLUE,
		      sampleSize);

	p->safePrintf("<input type=\"hidden\" "
		      "name=\"%s\" value=\"1\">", 
		      s_magicStr);
	p->safePrintf("<input type=\"hidden\" "
		      "name=\"dontlog\" value=\"1\">");

	p->safePrintf("</td></tr>");

	// . count the number of hosts we are getting logs for:
	int32_t numOn = 0;
	for (int32_t i = 0 ; i < nh ; i++ ) {
		char hostbuf[128];
		sprintf(hostbuf, "h%"INT32"",i);
		numOn += r->getLong(hostbuf, 0);
	}
	int32_t dfault = 0;
	if(numOn == 0) {
		dfault = 1;
		numOn = nh;
	}

	
	st->m_readBufSize = numOn * sampleSize;
	st->m_readBuf = (char*)mmalloc( st->m_readBufSize, "PageLogViewB");
	if(!st->m_readBuf) {
		g_httpServer.sendErrorReply(st->m_s,
					    500,"Out of Memory");
		delete st;
		mdelete(st, sizeof(StateLogView), "StateLogViewA");
		return true;

	}

	st->m_lastPtr = st->m_readBuf;


	st->m_filterStr[0] = "";
	st->m_filterStr[1] = "";
	st->m_filterStr[2] = "";
	st->m_filterStr[3] = "LOGIC";
	st->m_filterStr[4] = "DEBUG";
	st->m_filterStr[5] = "WARN";
	st->m_filterStr[6] = "INFO";
	st->m_filterStr[7] = "INIT";

	p->safePrintf("<tr bgcolor=#%s><td>Filter Types:</td><td>",
		      LIGHT_BLUE);
	char *checked;
	st->m_numFilts = 0;
	for(int32_t i = 7; i >= 0; i--) {
		char tmpbuf[128];
		sprintf(tmpbuf, "f%"INT32"", i);
		st->m_filters[i] =  r->getLong(tmpbuf, 0);
		if(st->m_filters[i]) {
			checked = "checked";
			st->m_numFilts++;
		}
		else checked = "";

		p->safePrintf("<input type=\"checkbox\" name=\"f%"INT32"\""
			      " value=\"1\" %s id=\"f%"INT32"\">", i, checked, i);
		if(i < 3) {
			char filtbuf[128];
			int32_t len;
			sprintf(filtbuf, "fs%"INT32"", i);
			st->m_filterStr[i] = r->getString(filtbuf, &len, "");
			if(len != 0) {
				gbmemcpy(st->m_lastPtr, st->m_filterStr[i], len);
				st->m_filterStr[i] = st->m_lastPtr;
				st->m_lastPtr += len;
				*(st->m_lastPtr) = '\0';
				(st->m_lastPtr)++;
			}
			else if(st->m_filters[i]) {
				st->m_filters[i] = false;
				st->m_numFilts--;
			}

			p->safePrintf("<input type=\"text\""
				      " name=\"fs%"INT32"\" "
				      "value=\"%s\" size=\"8\">", 
				      i, st->m_filterStr[i]); 
		}
		else p->safePrintf("%s",st->m_filterStr[i]);

	}
	p->safePrintf("<input type=\"button\""
		      " value=\"toggle\" "
		      "onclick=\"checkAll(this, 'f', 8);\">");
	p->safePrintf("</td></tr>\n");





	p->safePrintf("<tr bgcolor=#%s><td>Hosts:</td><td>",
		      LIGHT_BLUE);
	for ( int32_t i = 0 ; i < nh ; i++ ) {
		// skip dead hosts, i don't want to wait for them to timeout.
		if ( g_hostdb.isDead ( i ) ) continue;
		// get the ith host (hostId)
		char hostbuf[128];
		sprintf(hostbuf, "h%"INT32"",i);
		int32_t thisHost = r->getLong(hostbuf, dfault);
		if(!thisHost) {
			p->safePrintf("<input type=checkbox name=%s"
				      " value=1 id=\"%s\">%"INT32"", 
				      hostbuf,hostbuf, i);
			continue;
		}
		p->safePrintf("<input type=\"checkbox\" name=\"%s\""
			      " value=\"1\" id=\"%s\" checked>%"INT32"", 
			      hostbuf, hostbuf,i);
		if(!Msg1f::getLog(i, 
				  sampleSize, 
				  st,
				  gotRemoteLogWrapper)) {
			st->m_numOutstanding++;
			blocked = false;
		}
	}
	p->safePrintf("<input type=\"button\""
		      " value=\"toggle\" "
		      "onclick=\"checkAll(this, 'h', %"INT32");\">",nh);
	
	p->safePrintf("</td></tr>\n");
	
	p->safePrintf("<tr bgcolor=#%s><td>\n",LIGHT_BLUE);
	p->safePrintf("<input type=\"submit\" value=\"Update\"> ");
	p->safePrintf("</td><td></td></tr></table>\n");
	p->safePrintf("</form>");

	if(!blocked)
		return blocked;
	
	gotRemoteLogWrapper(st, NULL);
	return true;
}


bool showLine ( SafeBuf *sb , char *s , int32_t len ) {

	return sb->brify ( s , len , 
			   0 , // niceness 
			   8000 , // cols
			   "<br>",
			   false ); // isHtml?
}


void gotRemoteLogWrapper(void *state, UdpSlot *slot) {
	StateLogView* st = (StateLogView*)state;
	if(slot && !g_errno) {
		char *nextLine = strchr(slot->m_readBuf, '\n');
		if(nextLine) {
			int32_t segSize = slot->m_readBufSize - 
				(nextLine - slot->m_readBuf);
			if(segSize + st->m_lastPtr > 
			   st->m_readBufSize + st->m_readBuf)
				goto noRoom;

			gbmemcpy(st->m_lastPtr, 
			       nextLine + 1, 
			       segSize);
			st->m_readBufPtrs[st->m_numSlots] = st->m_lastPtr;
			st->m_lastPtr += slot->m_readBufSize;
			st->m_numSlots++;
		}
	}
 noRoom:

	if(--st->m_numOutstanding > 0) return;



	if(st->m_numSlots == 0) {
		g_httpServer.sendErrorReply(st->m_s,
					    500,"couldn't get logs");
		mfree(st->m_readBuf, st->m_readBufSize,"PageLogViewB");
		delete st;
		mdelete(st, sizeof(StateLogView), "StateLogViewA");
		return;
	}



	SafeBuf *p = &st->m_sb;
	//	p->safePrintf("	<font color=red>");

	p->safePrintf("<pre>");



	while(1) {
		int64_t timeStamp = 9223372036854775807LL;
		//int64_t timeStamp = LONG_LONG_MAX;
		int32_t ndx = -1;
		//get next winner
		for (int32_t i = 0; i < st->m_numSlots; i++) {
			if(!st->m_readBufPtrs[i]) continue;
			
			int64_t t = atoll(st->m_readBufPtrs[i]);
			if(t > timeStamp) continue;
			timeStamp = t;
			ndx = i;
		}
		if(ndx == -1) {
			p->safePrintf("</pre>");
			//			p->safePrintf("	</font>");
			p->safePrintf("</body>");
			p->safePrintf("</html>");
			char* sbuf = (char*) p->getBufStart();
			int32_t sbufLen = p->length();
			g_httpServer.sendDynamicPage(st->m_s, 
						     sbuf,
						     sbufLen,
						     -1/*cachetime*/);
			mfree(st->m_readBuf, st->m_readBufSize,"PageLogViewB");
			delete st;
			mdelete(st, sizeof(StateLogView), "StateLogViewA");

			return;
		}
		char *nextLine = strchr(st->m_readBufPtrs[ndx], '\n');
		int32_t lineLen;
		if(!nextLine)
			lineLen = gbstrlen(st->m_readBufPtrs[ndx]);
		else
			lineLen = nextLine - st->m_readBufPtrs[ndx];

		
		int32_t matchNum = -1;
		if(strnstr(st->m_readBufPtrs[ndx], 
			   s_magicStr, 
			   lineLen)) {
			goto skipPrint;
		}

		for(int32_t i = 0; i < 8; i++) {
			if(!st->m_filters[i]) continue;
			if(strnstr(st->m_readBufPtrs[ndx], 
				   st->m_filterStr[i], 
				   lineLen)){
				matchNum = i;
				break;
			}
		}
		if(matchNum >= 0 || st->m_numFilts == 0) {
			if(matchNum == 0) {
				p->safePrintf("<font color=red>");
				showLine(p,st->m_readBufPtrs[ndx], lineLen);
				p->safePrintf("\n");
				p->safePrintf("</font>");
			}
			else if(matchNum == 1) {
				p->safePrintf("<font color=green>");
				showLine(p,st->m_readBufPtrs[ndx], lineLen);
				p->safePrintf("\n");
				p->safePrintf("</font>");

			}
			else if(matchNum == 2) {
				p->safePrintf("<font color=blue>");
				showLine(p,st->m_readBufPtrs[ndx], lineLen);
				p->safePrintf("\n");
				p->safePrintf("</font>");
			}
			else {
				showLine(p,st->m_readBufPtrs[ndx], lineLen);
				p->safePrintf("\n");
			}
		}
	skipPrint:
		if(nextLine)
			st->m_readBufPtrs[ndx] = nextLine + 1;
		else
			st->m_readBufPtrs[ndx] = NULL;
	}

}
