#include "gb-include.h"

#include "HttpServer.h"
#include "Conf.h"
#include "Pages.h"
#include "Thesaurus.h"

void trimWhite(char *beginning);	// AutoBan.cpp, this should really be
				        // a utility function somewhere

bool sendPageThesaurus( TcpSocket *s, HttpRequest *r ) {
	SafeBuf p;
	char getBuf[64]; // holds extra values for GET method
	char formBuf[256]; // holds extra values for forms
	snprintf(getBuf, 64, "c=%s", 
		 r->getString("c", 0, ""));
	snprintf(formBuf, 256, 
		 "<input type=hidden name=\"c\" value=\"%s\">",
		 //"<input type=hidden name=\"pwd\" value=\"%s\">",
		 r->getString("c", 0, ""));
	g_pages.printAdminTop( &p, s, r);
	
	if (r->getLong("cancel", 0) != 0) {
		g_thesaurus.cancelRebuild();
		p.safePrintf("<br><br>\n");
		p.safePrintf(
		  "<center><b><font color=#ff0000>"
		  "rebuild canceled"
		  "</font></b></center>");
	}

	if (r->getLong("rebuild", 0) != 0) {
		bool full = r->getLong("full", 0);
		p.safePrintf("<br><br>\n");
		if (g_thesaurus.rebuild(0, full)) {
			p.safePrintf(
			  "<center><b><font color=#ff0000>"
			  "error starting rebuild, check log for details"
			  "</font></b></center>");
		} else {
			p.safePrintf(
			  "<center><b><font color=#ff0000>"
			  "rebuild started"
			  "</font></b></center>");
		}
	}
	
	if (r->getLong("rebuildaff", 0) != 0) {
		bool full = r->getLong("full", 0);
		p.safePrintf("<br><br>\n");
		if (g_thesaurus.rebuildAffinity(0, full)) {
			p.safePrintf(
			  "<center><b><font color=#ff0000>"
			  "error starting rebuild, check log for details"
			  "</font></b></center>");
		} else {
			p.safePrintf(
			  "<center><b><font color=#ff0000>"
			  "rebuild started"
			  "</font></b></center>");
		}
	}

	if (r->getLong("distribute", 0) != 0) {
		char cmd[1024];
		p.safePrintf("<br><br>\n");
		if (g_thesaurus.m_affinityState) {
			p.safePrintf(
			  "<center><b><font color=#ff0000>"
			  "cannot distribute during rebuild"
			  "</font></b></center>");
		} else {
			for ( int32_t i = 0; i < g_hostdb.getNumHosts() ; i++ ) {
				Host *h = g_hostdb.getHost(i);
				snprintf(cmd, 512,
					"rcp -r "
					"./dict/thesaurus.* "
					"%s:%s/dict/ &",
					iptoa(h->m_ip),
					h->m_dir);
				log(LOG_INFO, "admin: %s", cmd);
				system( cmd );
			}
			p.safePrintf(
			  "<center><b><font color=#ff0000>"
			  "data distributed"
			  "</font></b></center>");
		}	
	}

	if (r->getLong("reload", 0) != 0) {
		p.safePrintf("<br><br>\n");
		if (r->getLong("cast", 0) != 0) {
			p.safePrintf(
			  "<center><b><font color=#ff0000>"
			  "reload command broadcast"
			  "</font></b></center>");
		} else if (g_thesaurus.init()) {
			p.safePrintf(
			  "<center><b><font color=#ff0000>"
			  "thesaurus data reloaded"
			  "</font></b></center>");
		} else {
			p.safePrintf(
			  "<center><b><font color=#ff0000>"
			  "error reloading thesaurus data"
			  "</font></b></center>");
		}
	}

	int32_t manualAddLen = 0;
	char *manualAdd = NULL;
	SafeBuf manualAddBuf;
	if ((manualAdd = r->getString("manualadd", &manualAddLen))) {
		trimWhite(manualAdd);
		manualAddLen = gbstrlen(manualAdd);
		File manualFile;
		manualFile.set(g_hostdb.m_dir, "dict/thesaurus-manual.txt");
		if (manualFile.open(O_WRONLY | O_CREAT | O_TRUNC) &&
			(manualFile.write(manualAdd, manualAddLen, 0) ==
			 manualAddLen)) {
			char newl = '\n'; // for write()
			if (manualAdd[manualAddLen-1] != '\n')
				manualFile.write(&newl, 1, manualAddLen);
			p.safePrintf(
			  "<center><b><font color=#ff0000>"
			  "updated manual add file sucessfully"
			  "</font></b></center>");
		} else {
			p.safePrintf(
			  "<center><b><font color=#ff0000>"
			  "error writing manual add file"
			  "</font></b></center>");
		}
	} else {
		char ff[PATH_MAX];
		snprintf(ff, PATH_MAX, "%sdict/thesaurus-manual.txt",
			g_hostdb.m_dir);
		if (manualAddBuf.fillFromFile(ff)) {
			if (*(manualAddBuf.getBuf()-1) != '\n')
				manualAddBuf.pushChar('\n');
			manualAdd = manualAddBuf.getBufStart();
			manualAddLen = manualAddBuf.length();
		}
	}

	int32_t affinityAddLen = 0;
	char *affinityAdd = NULL;
	SafeBuf affinityAddBuf;
	if ((affinityAdd = r->getString("affinityadd", &affinityAddLen))) {
		trimWhite(affinityAdd);
		affinityAddLen = gbstrlen(affinityAdd);
		File affinityFile;
		affinityFile.set(g_hostdb.m_dir, 
			"dict/thesaurus-affinity.txt");
		if (affinityFile.open(O_WRONLY | O_CREAT | O_TRUNC) &&
			(affinityFile.write(affinityAdd, affinityAddLen, 0) ==
			 affinityAddLen)) {
			char newl = '\n'; // for write()
			if (affinityAdd[affinityAddLen-1] != '\n')
				affinityFile.write(&newl, 1, affinityAddLen);
			p.safePrintf(
			  "<center><b><font color=#ff0000>"
			  "updated affinity add file sucessfully"
			  "</font></b></center>");
		} else {
			p.safePrintf(
			  "<center><b><font color=#ff0000>"
			  "error writing affinity add file"
			  "</font></b></center>");
		}
	} else {
		char ff[PATH_MAX];
		snprintf(ff, PATH_MAX, "%sdict/thesaurus-affinity.txt",
			g_hostdb.m_dir);
		if (affinityAddBuf.fillFromFile(ff)) {
			if (*(affinityAddBuf.getBuf()-1) != '\n')
				affinityAddBuf.pushChar('\n');
			affinityAdd = affinityAddBuf.getBufStart();
			affinityAddLen = affinityAddBuf.length();
		}
	}
	

	char *syn = r->getString("synonym");
	int32_t len = 0;
	if (syn) len = gbstrlen(syn);

	if (len) {
		SynonymInfo info;
		bool r = g_thesaurus.getAllInfo(syn, &info, len, SYNBIT_ALL);
		p.safePrintf("<br><br>\n");
		p.safePrintf ( 
		  "<table cellpadding=4 width=100%% bgcolor=#%s border=1>"
		  "<tr>"
		  "<td colspan=2 bgcolor=#%s>"
		  "<center><b>Synonym List (%"INT32")</b></center>"
		  "</td>"
		  "</tr>\n",
		  LIGHT_BLUE, DARK_BLUE, info.m_numSyns);
		if (r) {
			p.safePrintf("<tr>"
			  "<td align=right><tt>%s</tt></td>"
			  "<td align=left>"
			  "<tt>1.000/%08lX (1.000/%08lX)</tt>"
			  "</td>"
			  "</tr>\n", syn, MAX_AFFINITY, MAX_AFFINITY);
			for (int32_t i = 0; i < info.m_numSyns; i++) {
				// get the reverse affinity as well
				int32_t aff = g_thesaurus.getAffinity(
					info.m_syn[i], syn,
					info.m_len[i], len);
				p.safePrintf( 
				  "<tr>"
				  "<td width=40%% align=right>"
				  "<tt>");
				p.safeMemcpy(info.m_syn[i], info.m_len[i]);
				p.safePrintf("</tt>"
				  "</td>"
				  "<td width=60%% align=left>"
				  "<tt>");
				if (info.m_affinity[i] >= 0) {
					p.safePrintf("%0.3f/%08lX ",
				  	  (float)info.m_affinity[i] 
					  	/ MAX_AFFINITY,
					  info.m_affinity[i]);
				} else {
					p.safePrintf("u ");
				}
				if (aff >= 0) {
					p.safePrintf("(%0.3f/%08lX) ",
					  (float)aff / MAX_AFFINITY, 
					  aff);
				} else {
					p.safePrintf("(u) ");
				}
				p.safePrintf("(%"INT32") (%"INT32") (%"INT32") (%"INT32") "
					     "(%lld) (%lld)",
				  (int32_t)info.m_type[i], (int32_t)info.m_sort[i],
				  info.m_firstId[i], info.m_lastId[i],
				  info.m_leftSynHash[i], 
				  info.m_rightSynHash[i]);
				for (int j = info.m_firstId[i]; 
					j <= info.m_lastId[i];
					j++) {
					p.safePrintf(" (%lld)",
						info.m_termId[j]);
				}
				p.safePrintf(
				  "</tt>"
				  "</td>"
				  "</tr>\n");
			}
			p.safePrintf("</table>");
		} else {
			p.safePrintf("<tr>"
			  "<td align=center><font color=#FF0000>"
			  "synonym not found: %s"
			  "</font></td>"
			  "</tr>\n",
			  syn);
		}
	}

	p.safePrintf ( "<br><br>\n" );

	p.safePrintf ( 
		  "<table cellpadding=4 width=100%% bgcolor=#%s border=1>"
		  "<tr>"
		  "<td colspan=2 bgcolor=#%s>"
		  "<center><b>Thesaurus Controls"
		  "</b></center></td>"
		  "</tr>\n",
		  LIGHT_BLUE, DARK_BLUE);
	
	p.safePrintf (
		  "<tr>"
		  "<td width=37%%><b>rebuild all data</b><br>"
		  "<font size=1>"
		  "rebuilds synonyms and then begins the rebuild process for "
		  "affinity data; this should only be run on one host, as the "
		  "data is copied when the process is finished; full rebuild "
		  "does not use existing affinity data"
		  "</font>"
		  "</td>"
		  "<td width=12%% bgcolor=#0000ff>"
		  "<center><b><a href=\"/admin/thesaurus?rebuild=1&%s\">"
		  "rebuild all data</a> <a href=\"/admin/thesaurus?"
		  "rebuild=1&full=1&%s\">(full)</a></b></center>"
		  "</td>"
		  "</tr>\n", getBuf, getBuf);

	p.safePrintf (
		  "<tr>"
		  "<td width=37%%><b>distribute data</b><br>"
		  "<font size=1>"
		  "distributes all thesaurus data to all hosts, this is "
		  "normally done automatically but if there was a problem "
		  "with the copy, this lets you do it manually"
		  "</font>"
		  "</td>"
		  "<td width=12%% bgcolor=#0000ff>"
		  "<center><b><a href=\"/admin/thesaurus?distribute=1&%s\">"
		  "distribute data</a></b></center>"
		  "</td>"
		  "</tr>\n", getBuf);

	p.safePrintf (
		  "<tr>"
		  "<td width=37%%><b>reload data</b><br>"
		  "<font size=1>"
		  "reloads the synonyms and affinity table on this host only"
		  "</font>"
		  "</td>"
		  "<td width=12%% bgcolor=#0000ff>"
		  "<center><b>"
		  "<a href=\"/admin/thesaurus?reload=1&cast=0&%s\">"
		  "reload data</a></b></center>"
		  "</td>"
		  "</tr>\n", getBuf);

	p.safePrintf (
		  "<tr>"
		  "<td width=37%%><b>reload data (all hosts)</b><br>"
		  "<font size=1>"
		  "reloads the synonyms and affinity table on all hosts"
		  "</font>"
		  "</td>"
		  "<td width=12%% bgcolor=#0000ff>"
		  "<center><b>"
		  "<a href=\"/admin/thesaurus?reload=1&cast=1&%s\">"
		  "reload data (all hosts)</a></b></center>"
		  "</td>"
		  "</tr>\n", getBuf);

	p.safePrintf (
		  "<tr>"
		  "<td width=37%%><b>list synonyms</b><br>"
		  "<font size=1>"
		  "enter a word here to list all synonym entries and their "
		  "affinities"
		  "</font>"
		  "</td>"
		  "<td width=12%%>"
		  "<form action=\"/admin/thesaurus>\">"
		  "<input type=text name=synonym size=20>"
		  "<input type=submit value=Submit>"
		  "%s"
		  "</form></td>"
		  "</tr>\n", formBuf);
		
	p.safePrintf (
		  "<tr>"
		  "<td colspan=2 bgcolor=#%s>"
		  "<center><b>Affinity Controls"
		  "</b></center></td>"
		  "</tr>\n",
		  DARK_BLUE);

	p.safePrintf (
		  "<tr>"
		  "<td width=37%%><b>cancel running rebuild</b><br>"
		  "<font size=1>"
		  "cancels the rebuild and throws all intermediate data away"
		  "</font>"
		  "</td>"
		  "<td width=12%% bgcolor=#0000ff>"
		  "<center><b><a href=\"/admin/thesaurus?cancel=1&%s\">"
		  "cancel running rebuild</a></b></center>"
		  "</td>"
		  "</tr>\n", getBuf);
	
	p.safePrintf (
		  "<tr>"
		  "<td width=37%%><b>rebuild affinity only</b><br>"
		  "<font size=1>"
		  "begins the rebuild process for affinity data, has no "
		  "effect if a rebuild is already in progress; full rebuild "
		  "does not reuse existing affinity data"
		  "</font>"
		  "</td>"
		  "<td width=12%% bgcolor=#0000ff>"
		  "<center><b><a href=\"/admin/thesaurus?rebuildaff=1&%s\">"
		  "rebuild affinity</a> <a href=\"/admin/thesaurus?"
		  "rebuildaff=1&full=1&%s\">(full)</a></b></center>"
		  "</td>"
		  "</tr>\n", getBuf, getBuf);
	
	p.safePrintf (
		  "<tr>"
		  "<td colspan=2 bgcolor=#%s>"
		  "<center><b>Manual File Controls"
		  "</b></td>"
		  "</tr>\n",
		  DARK_BLUE);

	p.safePrintf (
		  "<tr>"
		  "<td align=center colspan=2>");
	
	p.safePrintf(
		  "<b>manually added pairs</b><br>\n"
		  "<font size=1>place word pairs here that should be linked "
		  "as synonyms, one pair per line, seperated by a pipe '|' "
		  "character, optionally followed by another pipe and a type "
		  "designation; any badly formatted lines will be silently "
		  "ignored</font><br>\n"
		  "<form action=\"/admin/thesaurus\" method=post>"
		  "<textarea name=\"manualadd\" rows=20 cols=80>");

	if (manualAdd && manualAddLen) {
		p.htmlEncode(manualAdd, manualAddLen, true);
	}
	
	p.safePrintf (
		  "</textarea><br>"
		  "<input type=submit value=Submit>"
		  "<input type=reset value=Reset>"
		  "%s"
		  "</form></td>"
		  "</tr>\n",
		  formBuf);

	
	p.safePrintf (
		  "<tr>"
		  "<td align=center colspan=2>"
		  "<b>affinity value overrides</b><br>\n"
		  "<font size=1>place word/phrase pairs here that should have "
		  "there affinity values overridden, format is "
		  "\"word1|word2|value\", where value is a floating point, "
		  "integer (either decimal or hex), or the word \"max\"; "
		  "any badly formatted lines will be silently ignored; note "
		  "that these pairs will only work if the thesaurus otherwise "
		  "has an entry for them, so add them to the manual add file "
		  "above if need be</font><br>\n"
		  "<form action=\"/admin/thesaurus\" method=post>"
		  "<textarea name=\"affinityadd\" rows=20 cols=80>");

	if (affinityAdd && affinityAddLen) {
		p.htmlEncode(affinityAdd, affinityAddLen, true);
	}
	
	p.safePrintf (
		  "</textarea><br>"
		  "<input type=submit value=Submit>"
		  "<input type=reset value=Reset>"
		  "%s"
		  "</form></td>"
		  "</tr>\n", 
		  formBuf);


	p.safePrintf ( "</table>\n" );
	p.safePrintf ( "<br><br>\n" );

	p.safePrintf (
		  "<table cellpadding=4 width=100%% bgcolor=#%s border=1>"
		  "<tr>"
		  "<td colspan=2 bgcolor=#%s>"
		  "<center><b>Affinity Builder Status"
		  "</b></td>"
		  "</tr>\n",
		  LIGHT_BLUE, DARK_BLUE);

	int64_t a, b, c, d, e, f, g, h, i, j, k;
	StateAffinity *aff = g_thesaurus.m_affinityState;
	if (!aff) {
		p.safePrintf (
		  "<tr><td colspan=2>"
		  "<center><b>Not running</b></center>"
		  "</td></tr>\n");
		a = b = c = d = e = f = g = h = i = j = k = 0;
	} else {
		a = aff->m_oldTable->getNumSlotsUsed();
		b = aff->m_oldTable->getNumSlotsUsed() - aff->m_n;
		c = aff->m_n;
		d = (gettimeofdayInMilliseconds() - aff->m_time) / 1000;
		if (!d || !(c / d)) { 
			e = 0;
		} else {
			e = b / (c / d);
		}
		f = aff->m_sent;
		g = aff->m_recv;
		h = aff->m_errors;
		i = aff->m_old;
		j = aff->m_cache;
		k = aff->m_hitsTable.getNumSlotsUsed();
	}
	p.safePrintf (
		  "<tr><td><b># of total pairs</b></td>"
		  "<td>%"INT64"</td></tr>\n"
		  "<tr><td><b># of pairs remaining</b></td>"
		  "<td>%"INT64"</td></tr>\n"
		  "<tr><td><b># of pairs processed</b></td>"
		  "<td>%"INT64"</td></tr>\n"
		  "<tr><td><b>elapsed time in seconds</b></td>"
		  "<td>%"INT64"</td></tr>\n"
		  "<tr><td><b>estimated remaining time in seconds</b></td>"
		  "<td>%"INT64"</td></tr>\n"
		  "<tr><td><b># of requests sent</b></td>"
		  "<td>%"INT64"</td></tr>\n"
		  "<tr><td><b># of requests received</b></td>"
		  "<td>%"INT64"</td></tr>\n"
		  "<tr><td><b># of request errors</b></td>"
		  "<td>%"INT64"</td></tr>\n"
		  "<tr><td><b># of old values reused</b></td>"
		  "<td>%"INT64"</td></tr>\n"
		  "<tr><td><b># of cache hits</b></td>"
		  "<td>%"INT64"</td></tr>\n"
		  "<tr><td><b>cache size</b></td>"
		  "<td>%"INT64"</td></tr>\n",
		  a, b, c, d, e, f, g, h, i, j, k);
	p.safePrintf ( "</table>\n" );

	return g_httpServer.sendDynamicPage ( s, p.getBufStart(), p.length() );
}

