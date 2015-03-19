// Copyright 2009, Gigablast Inc.

// . runs a series of tests on a gigablast instance
// . right now just performs injections to test parsing and indexing

#undef _XOPEN_SOURCE
#define _XOPEN_SOURCE 500

#include "gb-include.h"

#include "Test.h"
#include "Rdb.h"
#include "Spider.h"
#include "Msg1.h"
#include "Datedb.h"
#include "Pages.h"
#include "PingServer.h"
#include "Spider.h"
#include "Process.h"
#include "Placedb.h"
#include "Threads.h"
#include "Msge1.h"
#include "Parms.h"

//static void testWrapper ( int fd , void *state ) ;
static void injectedWrapper ( void *state ) ;

// the global class
Test g_test;

Test::Test() {
	m_urlBuf = NULL;
	m_isRunning = false;
	m_isAdding  = false;
	m_urlsAdded = 0;
	m_urlsIndexed = 0;
	//m_spiderLinks = true;//false;
	m_bypassMenuElimination = false;
	// assume if they just turn spiders on we use this
	//m_testDir = "test-spider";
}

// main.cpp calls g_repair.init()
bool Test::init ( ) {
	m_isRunning = false;
	m_isAdding  = false;
	m_urlsAdded = 0;
	m_urlsIndexed = 0;
	//if( ! g_loop.registerSleepCallback( 1 , NULL , testWrapper ) )
	//	return log("repair: Failed register callback.");
	// record current value
	//m_testSpiderEnabledSaved = g_conf.m_testSpiderEnabled;
	//m_testParserEnabledSaved = g_conf.m_testParserEnabled;
	return true;
}

void Test::reset ( ) {
	if ( m_urlBuf ) mfree ( m_urlBuf , m_urlEnd - m_urlBuf , "test999");
	//m_spiderLinks = true;//false;
	m_bypassMenuElimination = false;
}

// . call this once every second 
// . this is responsible for advancing from one g_repairMode to the next
//void testWrapper ( int fd , void *state ) {
//	// call it from the class
//	g_test.loop();
//}

char *Test::getTestDir ( ) {
	// sanity
	if ( g_conf.m_testSpiderEnabled && g_conf.m_testParserEnabled ) {
		char *xx=NULL;*xx=0; }
	if ( g_conf.m_testSpiderEnabled )
		return "test-spider";
	if ( g_conf.m_testParserEnabled )
		return "test-parser";
	// default if they just turn on spiders (spiders on cmd)
	//return "test-spider";
	//if ( ! m_testDir ) { char *xx=NULL;*xx=0; }
	char *xx=NULL;*xx=0;
	return NULL;
}

void Test::removeFiles ( ) {
	// reset
	m_errno = 0;

	if ( g_conf.m_testParserEnabled ) {
		// remove all old files for now to avoid system diffs
		log("test: removing old parse critical and run files from "
		    "last run.");
		//system ("rm /home/mwells/gigablast/test/parse*.?.*" );
		//system ("rm /home/mwells/gigablast/test/critical*.?.*" );
		char sbuf[1024];
		char *testDir = getTestDir();
		sprintf(sbuf,"rm %s/%s/run.?.*" ,
			g_hostdb.m_dir,testDir);
		system (sbuf);
		// use this one instead since rm doesn't always work
		sprintf(sbuf,"ls -1 %s/%s/ | grep parse | xargs --verbose "
			"-I xxx rm %s/%s/xxx" ,
			g_hostdb.m_dir,
			testDir ,
			g_hostdb.m_dir,
			testDir );
		log("test: %s",sbuf);
		system(sbuf);
		
		sprintf(sbuf,"ls -1 %s/%s/ | grep critical | xargs --verbose "
			"-I xxx rm %s/%s/xxx" ,
			g_hostdb.m_dir,
			testDir ,
			g_hostdb.m_dir,
			testDir );
		log("test: %s",sbuf);
		system(sbuf);
	}


	// do not crash for lack of quickpoll now
	int32_t saved = g_conf.m_useQuickpoll;
	g_conf.m_useQuickpoll = false;

	CollectionRec *cr = g_collectiondb.getRec("qatest123");

	// . reset the qatest collection to zero docs
	// . TODO: implement this. only allow it for qatest coll.
	// . kinda like Collectiondb::deleteRec() i guess but we need to
	//   preserve the parms!!
	// . deletetagdb = false
	if ( cr ) g_collectiondb.resetColl2 ( cr->m_collnum , 
					      cr->m_collnum ,
					      true );

	// reset event count
	//g_collectiondb.countEvents();

	// turn it back on
	g_conf.m_useQuickpoll = saved;
}


// come here once per second i guess
void Test::initTestRun ( ) {

	g_errno = 0;

	// . all hosts should have their g_conf.m_repairMode parm set
	// . it is global now, not collection based, since we need to
	//   lock down titledb for the scan and there could be recs from
	//   the collection we are repairing in titledb's rdbtree, which,
	//   when dumped, would mess up our scan.
	if ( ! g_conf.m_testSpiderEnabled && ! g_conf.m_testParserEnabled ) {
		char *xx=NULL;*xx=0; }

	// if both enabled, core
	if ( g_conf.m_testSpiderEnabled && g_conf.m_testParserEnabled ) {
		char *xx=NULL;*xx=0; }

	// if the power went off
	if ( ! g_process.m_powerIsOn ) return;

	// return if currently running
	// no, admin can re-init even if running now
	//if ( m_isRunning ) { char *xx=NULL;*xx=0; }//return;

	// must be host #0 only
	if ( g_hostdb.m_myHost->m_hostId != 0 ) return;
	
	// if was initially in this mode, don't do anything
	//if ( m_testSpiderEnabledSaved ) return;
	//if ( m_testParserEnabledSaved ) return;

	// you must have the "qatest123" coll already setup!
	CollectionRec *cr = g_collectiondb.getRec("qatest123");
	if ( ! cr ) {
		// note it
		log("test: please add a collection named \"test\" first.");
		// stop the test
		g_conf.m_testParserEnabled = false;
		g_conf.m_testSpiderEnabled = false;
		// all done
		return;
	}

	char *testDir = getTestDir();

	// scan for file named "run.start.%"INT32".txt" which is a dump of all
	// the conf and parms 
	char filename[100];
	File f;
	int32_t i; for ( i = 0 ; i < 9999 ; i++ ) {
		// make filename. base it off working dir, g_hostdb.m_dir
		sprintf ( filename,"%s/%s/run.%"INT32".collparms.txt",
			  g_hostdb.m_dir,testDir,i );
		// exist?
		f.set ( filename );
		// open files
		int32_t status = f.doesExist();
		// error?
		if ( status == -1 ) {
			// note it in the log
			log("test: doesExist() returned -1");
			// end the test
			g_conf.m_testParserEnabled = false;
			g_conf.m_testSpiderEnabled = false;
			// all done
			return;
		}
		// try next i if this one in use
		if ( status ) continue;
		// got one
		break;
	}
	// close it
	f.close();

	// create the run.%"INT32".version.txt file
	char cmd[1000];
	char vfile[200];
	sprintf(vfile,"%s/%s/run.%"INT32".version.txt",g_hostdb.m_dir,testDir,i);
	sprintf(cmd,
		"%s/gb -v >& %s ; "
		"echo -n \"RUN START TIME: \" >> %s ; "
		"date >> %s",
		g_hostdb.m_dir,vfile,
		vfile,
		vfile);
	system(cmd);


	// save it
	m_runId = i;

	cr = g_collectiondb.getRec ( "qatest123" );
	if ( ! cr ) {
		// and no more of this
		g_conf.m_testParserEnabled = false;
		g_conf.m_testSpiderEnabled = false;
		return;
	}
	// set these
	m_coll = cr->m_coll;

	// turn on spiders
	//cr->m_spideringEnabled = 1;

	// crap i guess this too!!!
	//g_conf.m_spideringEnabled = 1;


	//
	// log out the global parms
	//
	char fbuf[100]; 
	// print our global parms into a file called run.%"INT32".start.txt
	sprintf(fbuf,"%s/%s/run.%"INT32".confparms.txt",g_hostdb.m_dir,testDir,i);
	// this saves it as xml i think
	g_parms.saveToXml ( (char *)&g_conf , fbuf , OBJ_CONF);

	//
	// log out the coll specific parms
	//
	// update name
	sprintf(fbuf,"%s/%s/run.%"INT32".collparms.txt",g_hostdb.m_dir,testDir,i);
	// save that
	g_parms.saveToXml ( (char *)cr , fbuf , OBJ_COLL);

	// get the list of urls to download and inject in order
	sprintf(fbuf,"%s/%s/urls.txt",g_hostdb.m_dir,testDir);
	// set it
	f.set ( fbuf ) ;
	// read it in
	int32_t fsize = f.getFileSize();
	// add one for \0 termination
	int32_t need = fsize + 1;
	// read it in
	char *buf = (char *)mmalloc ( need ,"qatest");
	// error?
	if ( ! buf ) {
		// note it
		log("test: failed to alloc %"INT32" bytes for url buf",fsize);
		// disable testing
		g_conf.m_testParserEnabled = false;
		g_conf.m_testSpiderEnabled = false;
		// all done
		return;
	}
	// open it
	f.open ( O_RDONLY );
	// read it in
	int32_t rs = f.read ( buf , fsize , 0 ) ;
	// check it
	if ( rs != fsize ) {
		// note it
		log("test: failed to read %"INT32" bytes of urls.txt file",fsize);
		// disable testing
		g_conf.m_testParserEnabled = false;
		g_conf.m_testSpiderEnabled = false;
		// all done
		return;
	}
	// save it
	m_urlBuf = buf;
	// null term it just in case
	buf[need-1] = '\0';
	// end of it, including the terminating \0
	m_urlEnd = buf + need;
	// init url offset
	m_urlPtr = m_urlBuf;

	// reset just in case
	//m_spiderLinks = false;
	m_bypassMenuElimination = false;

	// first check for spiderlinks=1|true
	for ( char *p = m_urlBuf ; p < m_urlEnd ; p++ ) {
		//if ( p[0] != 's' ) continue;
		//if ( p[1] != 'p' ) continue;
		//if ( ! strncmp(p,"spiderlinks",11) ) 
		//	m_spiderLinks = true;
		//if ( ! strncmp(p,"bypassmenuelimination",21) ) 
		//	m_bypassMenuElimination = true;
	}

	// force max spiders to one because one page is often dependent
	// on the previous page!
	//if ( ! m_spiderLinks ) cr->m_maxNumSpiders = 1;
	// need to make it 6 since some priorities essentially lock the
	// ips up that have urls in higher priorities. i.e. once we dole 
	// a url out for ip X, then if later we add a high priority url for
	// IP X it can't get spidered until the one that is doled does.
	//else                   cr->m_maxNumSpiders = 6;

	// . first space out all comments
	// . comments are nice because we know why the url is in urls.txt
	for ( char *p = m_urlBuf ; p < m_urlEnd ; p++ ) {
		// skip if not start of a comment line
		if ( *p != '#' ) continue;
		// if not preceeded by a \n or start, skip
		if ( p > m_urlBuf && *(p-1) != '\n' ) continue;
		// ok, nuke it
		for ( ; *p && *p !='\n' ; p++ ) *p = ' ';
	}

	// if we hit "\nSTOP\n" then white out that and all past it
	for ( char *p = m_urlBuf ; p < m_urlEnd ; p++ ) {
		// skip if not start of a comment line
		if ( *p != '\n' ) continue;
		// check it
		if ( strncmp(p,"\nSTOP\n",6) ) continue;
		// white out
		for ( ; *p ; p++ ) {
			// until we HIT RESUME
			if ( *p == '\n' && ! strncmp(p,"\nRESUME\n",8) ) {
				p[1] = ' ';
				p[2] = ' ';
				p[3] = ' ';
				p[4] = ' ';
				p[5] = ' ';
				p[6] = ' ';
				break;
			}
			*p = ' ';
		}
		// all done
		//break;
	}

	// then NULL terminate all urls by converting all white space to \0s
	for ( char *p = m_urlBuf ; p < m_urlEnd ; p++ )
		// all non url chars to \0
		if ( is_wspace_a(*p) ) *p = '\0';
	

	// flag this
	m_isRunning = true;

	// and this
	m_isAdding = true;

	m_testStartTime = gettimeofdayInMilliseconds();

	// set up dedup table
	m_dt.set ( 8,0,0,NULL,0,false,MAX_NICENESS,"testdedup");

	// remove all old files for now to avoid system diffs
	log("test: beginning injection");

	// . now inject each url in order, one at a time using msg7 i guess
	// . returns true if all done
	if ( ! injectLoop() ) return;
	// close it up
	//stopIt();
}


// this should be called when all docs have finished spidering
void Test::stopIt ( ) {

	// sanity
	if ( m_isAdding ) { char *xx=NULL;*xx=0; }
	// flag that we are done
	m_isRunning = false;

	// print time
	log("test: took %"INT64" ms to complete injections.",
	    gettimeofdayInMilliseconds() - m_testStartTime );

	// get this before setting testParserEnabled to false
	char *testDir = g_test.getTestDir();

	// turn this off now too
	g_conf.m_testParserEnabled = false;
	g_conf.m_testSpiderEnabled = false;



	// save all!
	bool disabled = g_threads.m_disabled;
	g_threads.disableThreads();
	// save it blocking style
	g_process.save();
	if ( ! disabled ) g_threads.enableThreads();

	// save ips.txt
	saveTestBuf ( testDir );

	log("test: test completed. making qa.html");

	//
	//
	// NOW MAKE THE qa.html FILE
	//
	//

	// only analyze up to last 7 runs
	int32_t start = m_runId - 7;
	if ( start < 0 ) start = 0;

	SafeBuf sb;
	sb.safePrintf("<table border=1>\n");
	sb.safePrintf("<tr>"
		      "<td><b><nobr>run id</nobr></b></td>"
		      "<td><b><nobr>conf diff</nobr></b></td>"
		      "<td><b><nobr>coll diff</nobr></b></td>"
		      "<td><b><nobr>run info</nobr></b></td>"
		      "</tr>\n");

	// take diffs between this run and the last run for confparms
	for ( int32_t i = m_runId ; i > start ; i-- ) {
		// int16_tcut
		char *dir = g_hostdb.m_dir;
		// make diff filename
		char diff1[200];
		sprintf(diff1,"%s/%s/run.%"INT32".confparms.txt.diff",dir,
			testDir,i);
		File f1;
		f1.set(diff1);
		if ( ! f1.doesExist() ) {
			char df1[200];
			char df2[200];
			sprintf(df1,"%s/%s/run.%"INT32".confparms.txt",dir,
				testDir,i);
			sprintf(df2,"%s/%s/run.%"INT32".confparms.txt",dir,
				testDir,i-1);
			// do the diff
			char cmd[600];
			sprintf(cmd,"diff %s %s > %s",df1,df2,diff1);
			log("test: system(\"%s\")",cmd);
			system (cmd);
		}
		int32_t fs1 = f1.getFileSize();
		sb.safePrintf("<tr><td>%"INT32"</td><td>%"INT32"</td>", i,fs1);

		// make diff filename
		char diff2[200];
		sprintf(diff2,"%s/%s/run.%"INT32".collparms.txt.diff",dir,
			testDir,i);
		File f2;
		f2.set(diff2);
		if ( ! f2.doesExist() ) {
			char df1[200];
			char df2[200];
			sprintf(df1,"%s/%s/run.%"INT32".collparms.txt",dir,
				testDir,i);
			sprintf(df2,"%s/%s/run.%"INT32".collparms.txt",dir,
				testDir,i-1);
			// do the diff
			char cmd[600];
			sprintf(cmd,"diff %s %s > %s",df1,df2,diff2);
			log("test: system(\"%s\")",cmd);
			system (cmd);
		}
		int32_t fs2 = f2.getFileSize();
		sb.safePrintf("<td>%"INT32"</td>", fs2);

		// the version
		char vf[200]; 
		sprintf(vf,"%s/%s/run.%"INT32".version.txt",dir,testDir,i);
		File f3; 
		f3.set ( vf );
		int32_t fs3 = f3.getFileSize();
		char vbuf[1000];
		vbuf[0] = 0;
		if ( fs3 > 0 ) {
			f3.open(O_RDONLY);
			int32_t rs = f3.read(vbuf,fs3,0);
			vbuf[fs3] = '\0';
			if ( rs <= 0 ) continue;
			f3.close();
		}
		// show it
		sb.safePrintf("<td><pre>%s</pre></td></tr>\n", vbuf);
	}
	sb.safePrintf("</table>\n");
	sb.safePrintf("<br>\n");


	//
	// now diff each parser output file for each url in urls.txt
	//


	//
	// loop over url buf first so we can print one table per url
	//

	char *next = NULL;
	// reset the url buf ptr
	m_urlPtr = m_urlBuf;
	// count em
	int32_t count = 0;

	// ptrs to each url table
	int32_t  un = 0;
	int32_t  uptr [5000]; // offsets now, not char ptr since buf gets reallocd
	char  udiff[5000];
	int32_t  ulen [5000];
	int32_t  uhits[5000]; // critical errors! validateOutput() choked!
	int32_t  uunchecked[5000]; // events/addresses found but were not validatd
	int32_t  umiss[5000];
	int32_t  usort[5000];
	int32_t  uevents[5000];
	SafeBuf tmp;

	int32_t niceness = MAX_NICENESS;

	// advance to next url
	for ( ; m_urlPtr < m_urlEnd ; m_urlPtr = next ) {
		// breathe
		QUICKPOLL(niceness);
		// we converted all non-url chars into \0's so skip those!
		for ( ; m_urlPtr<m_urlEnd && !*m_urlPtr ; m_urlPtr++ );
		// breach check
		if ( m_urlPtr >= m_urlEnd ) break;
		// set this up
		next = m_urlPtr;
		// compute next url ptr
		for ( ; next < m_urlEnd && *next ; next++ );
		// point to this url
		char *u = m_urlPtr;
		// get hash
		int64_t h = hash64 ( u , gbstrlen(u) );
		// int16_tcut
		char *dir = g_hostdb.m_dir;


		// print into a secondary safe buf with a ptr to
		// it so we can sort that and transfer into the
		// primary safebuf later
		uptr[un] = tmp.length();
		// assume no diff
		udiff[un] = 0;

		// print number
		tmp.safePrintf("%"INT32") ",count++);
		// . link to our stored http server reply
		// . TODO: link it to our [cached] copy in the test coll!!!
		char local[1200];
		sprintf(local,"/%s/doc.%"UINT64".html",testDir,h);
		tmp.safePrintf("<a href=\"%s\"><b>%s</b></a> ",local,u);
		// link to live page
		tmp.safePrintf(" <a href=\"%s\">live</a> ",u);
		// link to page parser
		char ubuf[2000];
		urlEncode(ubuf,2000,u,gbstrlen(u),true);
		tmp.safePrintf(" <a href=\"/admin/parser?c=test&"
			       "u=%s\">parser</a> ",ubuf);
		//tmp.safePrintf(" (%"UINT64")",h);
		tmp.safePrintf("<br>\n");
		//tmp.safePrintf("<br>\n");
		tmp.safePrintf("<table border=1>\n");
		tmp.safePrintf("<tr>"
			      "<td><b><nobr>run id</nobr></b></td>"
			      "<td><b><nobr>crit hits</nobr></b></td>"
			      "<td><b><nobr>crit errors</nobr></b></td>"
			      "<td><b><nobr># e</nobr></b></td>"
			      "<td><b><nobr>unchecked</nobr></b></td>"
			      "<td><b><nobr>diff chars</nobr></b></td>"
			      "<td><b><nobr>diff file</nobr></b></td>"
			      "<td><b><nobr>full output</nobr></b></td>"
			      "</tr>\n");

		//SafeBuf sd;

		// loop over all the runs now, starting with latest run first
		for ( int32_t ri = m_runId ; ri >= start ; ri-- ) {

			QUICKPOLL(niceness);

			// the diff filename
			char pdiff[200];
			sprintf(pdiff,"%s/%s/parse.%"UINT64".%"INT32".html.diff",dir,
				testDir,h,ri);
			File f;
			f.set(pdiff);
			int32_t fs = f.getFileSize();
			if ( ! f.doesExist() && ri > 0 ) {
				// make the parse filename
				char pbuf1[200];
				char pbuf2[200];
				sprintf(pbuf1,"%s/%s/parse.%"UINT64".%"INT32".html",
					dir,testDir,h,ri);
				sprintf(pbuf2,"%s/%s/parse.%"UINT64".%"INT32".html",
					dir,testDir,h,ri-1);
				// sanity check
				//File tf; tf.set(pbuf1);
				//if ( ! tf.doesExist()) {char *xx=NULL;*xx=0;}
				// tmp file name
				char tmp1[200];
				char tmp2[200];
				sprintf(tmp1,"%s/%s/t1.html",dir,testDir);
				sprintf(tmp2,"%s/%s/t2.html",dir,testDir);
				// filter first
				char cmd[600];
				sprintf(cmd,
					"cat %s | "
					"grep -v \"<!--ignore-->\" "
					" > %s", pbuf1,tmp1);
				system(cmd);
				sprintf(cmd,
					"cat %s | "
					"grep -v \"<!--ignore-->\" "
					" > %s", pbuf2,tmp2);
				system(cmd);
				// make the system cmd to do the diff
				sprintf(cmd,
					"echo \"<pre>\" > %s ; "
					"diff -w --text %s %s "
					// ignore this table header row
					//" | grep -v \"R#4\""
					" >> %s",
					pdiff,
					tmp1,tmp2,pdiff);
				log("test: system(\"%s\")",cmd);
				system(cmd);
				// try again
				f.set(pdiff);
				fs = f.getFileSize();
			}

			QUICKPOLL(niceness);

			// this means 0 . it just has the <pre> tag in it!
			if ( fs < 0 || fs == 6 ) fs = 0;
			// . if no diff and NOT current run, do not print it
			// . print it if the run right before the current 
			//   now always too
			if ( ri != m_runId && ri != m_runId-1 && fs == 0 ) 
				continue;
			// relative filename
			char rel[200];
			sprintf(rel,"/%s/parse.%"UINT64".%"INT32".html.diff",
				testDir,h,ri);
			char full[200];
			sprintf(full,"/%s/parse.%"UINT64".%"INT32".html",
				testDir,h,ri);
			char validate[200];
			sprintf(validate,
				"/%s/parse-int16_tdisplay.%"UINT64".%"INT32".html",
				testDir,h,ri);
			// use red font for current run that has a diff!
			char *t1 = "";
			char *t2 = "";
			if ( ri == m_runId && fs != 0 ) {
				t1 = "<font color=pink><b>";
				t2 = "</b></font>";
				// a diff
				udiff[un] = 1;
			}

			// . get critical errors
			// . i.e. XmlDoc::validateOutput() could not validate
			//   a particular event or address that was in the
			//   url's "validated.uh64.txt" file since the admin
			//   clicked on the checkbox in the page parser output
			// . if we do not find such a tag in the parser output
			//   any more then Spider.cpp creates this file!
			if ( ri == m_runId ) {
				char cfile[256];
				sprintf(cfile,"%s/%s/critical.%"UINT64".%"INT32".txt",
					g_hostdb.m_dir,testDir,h,ri);
				SafeBuf ttt;
				ttt.fillFromFile(cfile);
				// first int32_t is misses, then hits then events
				umiss[un] = 0;
				uhits[un] = 0;
				uevents[un] = 0;
				uunchecked[un] = 0;
				if ( ttt.length() >= 3 )
					sscanf(ttt.getBufStart(),
					       "%"INT32" %"INT32" %"INT32" %"INT32"",
					       &umiss[un],
					       &uhits[un],
					       &uevents[un],
					       &uunchecked[un]);
				usort[un] = umiss[un] + uunchecked[un];
				//File cf;
				//cf.set(cfile);
				//if ( cf.doesExist()) ucrit[un] = 1;
				//else                 ucrit[un] = 0;
			}

			// more critical?
			if ( ri == m_runId && umiss[un] != 0 ) {
				t1 = "<font color=red><b>";
				t2 = "</b></font>";
			}

			// . these are good to have
			// . if you don't have 1+ critical hits then you
			//   probably need to be validate by the qa guy
			char *uhb1 = "";
			char *uhb2 = "";
			if ( ri == m_runId && uhits[un] != 0 ) {
				uhb1 = "<font color=green><b>**";
				uhb2 = "**</b></font>";
			}

			QUICKPOLL(niceness);

			char *e1 = "<td>";
			char *e2 = "</td>";
			int32_t ne = uevents[un];
			if ( ne ) { 
				e1="<td bgcolor=orange><b><font color=brown>"; 
				e2="</font></b></td>"; 
			}
			char *u1 = "<td>";
			char *u2 = "</td>";
			if ( uunchecked[un] ) {
				u1="<td bgcolor=purple><b><font color=white>"; 
				u2="</font></b></td>"; 
			}
				
			// print the row!
			tmp.safePrintf("<tr>"
				      "<td>%s%"INT32"%s</td>"
				       "<td>%s%"INT32"%s</td>" // critical hits
				       "<td>%s%"INT32"%s</td>" // critical misses
				       "%s%"INT32"%s" // # events
				       "%s%"INT32"%s" // unchecked
				       "<td>%s%"INT32"%s</td>" // filesize of diff
				      // diff filename
				      "<td><a href=\"%s\">%s%s%s</a></td>"
				      // full parser output
				      "<td>"
				       "<a href=\"%s\">full</a> | "
				       "<a href=\"%s\">validate</a> "
				       "</td>"
				      "</tr>\n",
				      t1,ri,t2,
				      uhb1,uhits[un],uhb2,
				      t1,umiss[un],t2,
				      e1,ne,e2,
				       u1,uunchecked[un],u2,
				      t1,fs,t2,
				      rel,t1,rel,t2,
				      full,
				       validate);


			// only fill "sd" for the most recent guy
			if ( ri != m_runId ) continue;

			// now concatenate the parse-int16_tdisplay file
			// to this little table so qa admin can check/uncheck
			// validation checkboxes for addresses and events
			//sprintf(cfile,
			//	"%s/test/parse-int16_tdisplay.%"UINT64".%"INT32".html",
			//	g_hostdb.m_dir,h,ri);
			//sd.fillFromFile ( cfile );
		}
		// end table
		tmp.safePrintf("</table>\n");

		// . and a separate little section for the checkboxes
		// . should already be in tables, etc.
		// . each checkbox should provide its own uh64 when it
		//   calls senddiv() when clicked now
		//tmp.cat ( sd );

		tmp.safePrintf("<br>\n");
		tmp.safePrintf("<br>\n");
		// set this
		ulen[un] = tmp.length() - uptr[un] ;
		// sanity check
		if ( ulen[un] > 10000000 ) { char *xx=NULL;*xx=0; }
		// inc it
		un++;
		// increase the 5000!!
		if ( un >= 5000 ) { char *xx=NULL; *xx=0; }
	}


	char flag ;
 bubble:
	flag = 0;
	// sort the url tables
	for ( int32_t i = 0 ; i < un - 1 ; i++ ) {
		QUICKPOLL(niceness);
		if ( usort[i] >  usort[i+1] ) continue;
		if ( usort[i] == usort[i+1] ) 
			if ( udiff[i] >= udiff[i+1] ) continue;
		// swap em
		int32_t  tp = uptr[i];
		int32_t  td = udiff[i];
		int32_t  um = umiss[i];
		int32_t  us = usort[i];
		int32_t  uh = uhits[i];
		int32_t  tl = ulen [i];
		uptr[i] = uptr[i+1];
		umiss[i] = umiss[i+1];
		usort[i] = usort[i+1];
		uhits[i] = uhits[i+1];
		udiff[i] = udiff[i+1];
		ulen[i]  = ulen[i+1];
		uptr[i+1] = tp;
		umiss[i+1] = um;
		usort[i+1] = us;
		uhits[i+1] = uh;
		udiff[i+1] = td;
		ulen [i+1] = tl;
		flag = 1;
	}
	if ( flag ) goto bubble;

	// transfer into primary safe buf now
	for ( int32_t i = 0 ; i < un ; i++ ) 
		sb.safeMemcpy(tmp.getBufStart() + uptr[i],ulen[i]);


	sb.safePrintf("</html>\n");

	char dfile[200];
	sprintf(dfile,"%s/%s/qa.html",g_hostdb.m_dir,testDir);
	sb.dumpToFile ( dfile );

	// free the buffer of urls
	reset();

	// turn off spiders
	g_conf.m_spideringEnabled = 0;

	// all done
	return;
}


void injectedWrapper ( void *state ) {
	// wait for all msg4 buffers to flush
	//if ( ! flushMsg4Buffers ( state , injectedWrapper ) ) return;
	// this function is in Msge1.cpp. save ip file in test subdir
	//saveTestBuf();
	if ( ! g_test.injectLoop() ) return;
	//g_test.stopIt();
}

static int32_t s_count = 0;

// . returns true if all done!
// . returns false if still doing stuff
bool Test::injectLoop ( ) {

	int32_t  dlen   ;
	char *dom    ;
	int32_t  fakeIp ;

 loop:
	// advance to next url
	for ( ; m_urlPtr < m_urlEnd && ! *m_urlPtr ; m_urlPtr++ ) ;
	// all done?
	if ( m_urlPtr >= m_urlEnd ) {
		// flush em out
		if ( ! flushMsg4Buffers ( this , injectedWrapper ) ) 
			return false;
		// note it
		m_isAdding = false;
		// all done
		return true;
	}
	// error means all done
	if ( m_errno ) { m_isAdding = false; return true; }
	// point to it
	char *u = m_urlPtr;
	// advance to point to the next url for the next loop!
	for ( ; m_urlPtr < m_urlEnd && *m_urlPtr ; m_urlPtr++ ) ;

	// hash it
	int64_t h = hash64b ( u );
	// dedup it lest we freeze up and stopIt() never gets called because
	// m_urlsAdded is never decremented all the way to zero in Spider.cpp
	if ( m_dt.isInTable ( &h ) ) goto loop;
	// add it. return true with g_errno set on error
	if ( ! m_dt.addKey ( &h ) ) goto hadError;

	// make the SpiderRequest from it
	m_sreq.reset();
	// url
	strcpy ( m_sreq.m_url , u );
	// get domain of url
	dom = getDomFast ( m_sreq.m_url , &dlen );
	// make a fake ip
	fakeIp = 0x123456;
	// use domain if we got that
	if ( dom && dlen ) fakeIp = hash32 ( dom , dlen );
	// first ip is fake
	m_sreq.m_firstIp = fakeIp; // 0x123456;
	// these too
	m_sreq.m_domHash32  = fakeIp;
	m_sreq.m_hostHash32 = fakeIp;
	m_sreq.m_siteHash32 = fakeIp;
	//m_sreq.m_probDocId = g_titledb.getProbableDocId( m_sreq.m_url );
	// this crap is fake
	m_sreq.m_isInjecting = 1;
	// use test-spider subdir for storing pages and spider times?
	// MDW: this was replaced by m_isParentSiteMap bit.
	//if ( g_conf.m_testSpiderEnabled ) m_sreq.m_useTestSpiderDir = 1;
	// use this later
	m_sreq.m_hasContent = 0;
	// injected requests use this as the spider time i guess
	// so we can sort them by this
	m_sreq.m_addedTime = ++s_count;

	// no, because to compute XmlDoc::m_min/maxPubDate we need this to
	// be valid for our test run.. no no we will fix it to be
	// basically 2 days before spider time in the code...
	//m_sreq.m_addedTime = spiderTime;

	m_sreq.m_fakeFirstIp = 1;

	// make the key (parentDocId=0)
	m_sreq.setKey ( fakeIp, 0LL , false );
	// test it
	if ( g_spiderdb.getFirstIp(&m_sreq.m_key) != fakeIp ) {
		char *xx=NULL;*xx=0;}
	// sanity check. check for http(s)://
	if ( m_sreq.m_url[0] != 'h' ) { char *xx=NULL;*xx=0; }

	// reset this
	g_errno = 0;

	// count it
	m_urlsAdded++;

	// note it
	//log("crazyout: %s",m_sreq.m_url );
	logf(LOG_DEBUG,"spider: injecting test url %s",m_sreq.m_url);

	// the receiving end will realize that we are injecting into the test
	// collection and use the "/test/" subdir to load the file
	// "ips.txt" to do our ip lookups, and search for any downloads in
	// that subdirectory as well.
	if ( ! m_msg4.addMetaList ( (char *)&m_sreq     ,
				    m_sreq.getRecSize() ,
				    m_coll              ,
				    NULL                ,
				    injectedWrapper     ,
				    MAX_NICENESS        ,
				    RDB_SPIDERDB        ) )
		// return false if blocked
		return false;
	// error?
	if ( g_errno ) {
		// jump down here from above on error
	hadError:
		// save it
		m_errno = g_errno;
		// flag it
		m_isAdding = false;
		// note it
		log("test: inject had error: %s",mstrerror(g_errno));
		// stop, we are all done!
		return true;
	}
	// add the next spider request
	goto loop;
}
