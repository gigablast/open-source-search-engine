#include "gb-include.h"

#include "Process.h"
#include "Rdb.h"
//#include "Checksumdb.h"
#include "Clusterdb.h"
#include "Hostdb.h"
#include "Tagdb.h"
//#include "Catdb.h"
#include "Posdb.h"
#include "Cachedb.h"
#include "Monitordb.h"
#include "Datedb.h"
#include "Titledb.h"
//#include "Revdb.h"
#include "Sections.h"
#include "Spider.h"
#include "Statsdb.h"
//#include "Tfndb.h"
#include "Threads.h"
#include "PingServer.h"
#include "Dns.h"
#include "Repair.h"
#include "RdbCache.h"
#include "Spider.h"
//#include "Classifier.h"
//#include "PageTopDocs.h"
#include "HttpServer.h"
#include "Speller.h"
//#include "Thesaurus.h"
#include "Spider.h"
#include "Profiler.h"
#include "PageNetTest.h"
#include "LangList.h"
#include "AutoBan.h"
//#include "SiteBonus.h"
#include "Msg4.h"
#include "Msg5.h"
//#include "PageTurk.h"
//#include "Syncdb.h"
//#include "Placedb.h"
#include "Wiki.h"
#include "Wiktionary.h"
#include "Users.h"
#include "Proxy.h"

// the query log hashtable defined in XmlDoc.cpp
//extern HashTableX g_qt;
extern SafeBuf    g_qbuf;
extern long       g_qbufNeedSave;

// for resetAll()
//#include "Msg6.h"
extern void resetPageAddUrl    ( );
extern void resetHttpMime      ( );
extern void reset_iana_charset ( );
extern void resetAdultBit      ( );
extern void resetDomains       ( );
extern void resetEntities      ( );
extern void resetQuery         ( );
extern void resetStopWords     ( );
extern void resetAbbrTable     ( );
extern void resetUnicode       ( );


// our global instance
Process g_process;

//static long s_flag = 1;
static long s_nextTime = 0;

char *g_files[] = {
	"gb.conf",
	"hosts.conf",
	
	"catcountry.dat",
	"badcattable.dat",
	
	"ucdata/cd_data.dat",
	"ucdata/cdmap.dat",
	"ucdata/combiningclass.dat",
	"ucdata/kd_data.dat",
	"ucdata/kdmap.dat",
	"ucdata/lowermap.dat",
	"ucdata/properties.dat",
	"ucdata/scripts.dat",
	"ucdata/uppermap.dat",
	
	// called by gb via system() to convert non-html doc to html
	//"gbfilter",  
	
	// need for checking hard drive temperature
	//"/usr/sbin/hddtemp",
	
	// used by tagdb i guess
	//"top100000Alexa.txt",
	
	//"7za" ,  // 7-zip compression

	// 'gbfilter' calls these filters to convert various doc types
	// into html before being fed to parser
	"antiword" ,  // msword
	"pdftohtml",  // pdf
	"pstotext" ,  // postscript
	"ppthtml"  ,  // powerpoint
	
	//"dict/unifiedDict",
	//"dict/thesaurus.txt",
	
	// for spell checking
	//"dict/en/en_phonet.dat",
	//"dict/en/en.query.phonet",
	
	"antiword-dir/8859-1.txt",
	"antiword-dir/8859-10.txt",
	"antiword-dir/8859-13.txt",
	"antiword-dir/8859-14.txt",
	"antiword-dir/8859-15.txt",
	"antiword-dir/8859-16.txt",
	"antiword-dir/8859-2.txt",
	"antiword-dir/8859-3.txt",
	"antiword-dir/8859-4.txt",
	"antiword-dir/8859-5.txt",
	"antiword-dir/8859-6.txt",
	"antiword-dir/8859-7.txt",
	"antiword-dir/8859-8.txt",
	"antiword-dir/8859-9.txt",
	"antiword-dir/Default",
	"antiword-dir/Example",
	"antiword-dir/MacRoman.txt",
	"antiword-dir/UTF-8.txt",
	"antiword-dir/Unicode",
	"antiword-dir/cp1250.txt",
	"antiword-dir/cp1251.txt",
	"antiword-dir/cp1252.txt",
	"antiword-dir/cp437.txt",
	"antiword-dir/cp850.txt",
	"antiword-dir/cp852.txt",
	"antiword-dir/fontnames",
	"antiword-dir/fontnames.russian",
	"antiword-dir/koi8-r.txt",
	"antiword-dir/koi8-u.txt",
	"antiword-dir/roman.txt",
	
	// . thumbnail generation
	// . use 'apt-get install netpbm' to install
	//"/usr/bin/giftopnm",
	//"/usr/bin/tifftopnm",
	//"/usr/bin/pngtopnm",
	//"/usr/bin/jpegtopnm",
	//"/usr/bin/bmptopnm",
	//"/usr/bin/pnmscale",
	//"/usr/bin/ppmtojpeg",
	//"/usr/sbin/smartctl",

	"giftopnm",
	"tifftopnm",
	"pngtopnm",
	"jpegtopnm",
	"bmptopnm",
	"pnmscale",
	"ppmtojpeg",

	//"smartctl",

	"wikititles.txt.part1",
	"wikititles.txt.part2",
	
	//
	// this junk can be generated
	//
	//"wikiwords.dat",//enwikitionary.xml",
	//"zips.dat",
	//"timezones.dat",
	//"aliases.dat",
	//"cities.dat",
	
	NULL
};



bool Process::checkFiles ( char *dir ) {

	// check these by hand since you need one or the other
	File f1;
	File f2;
	File f3;
	File f4;
	f1.set ( dir , "allCountries.txt" );
	f2.set ( dir , "postalCodes.txt" );
	//f3.set ( dir , "places.dat" );
	f4.set ( dir , "zips.dat" );
	if ( //( ! f3.doesExist() || ! f4.doesExist() ) && 
	    ( ! f4.doesExist() ) && 
	     ( ! f1.doesExist() || ! f2.doesExist() ) ) {
		log("db: need either (%s and %s) or (%s and %s)",
		    f3.getFilename() ,
		    f4.getFilename() ,
		    f1.getFilename() ,
		    f2.getFilename() );
		//return false;
	}

	// check for email subdir
	//f1.set ( dir , "/html/email/");
	//if ( ! f1.doesExist() ) {
	//	log("db: email subdir missing. add html/email");
	//	return false;
	//}

	// make sure we got all the files
	//if ( ! g_conf.m_isLive ) return true;
	bool needsFiles = false;

	for ( long i = 0 ; i < (long)sizeof(g_files)/4 ; i++ ) {
		// terminate?
		if ( ! g_files[i] ) break;
		File f;
		char *dd = dir;
		if ( g_files[i][0] != '/' )
			f.set ( dir , g_files[i] );
		else {
			f.set ( g_files[i] );
			dd = "";
		}
		if ( ! f.doesExist() ) {
			log("db: %s%s file missing."
			    ,dd,g_files[i]);
			//log("db: %s%s missing. Copy over from "
			//    "titan:/gb/conf/%s",dd,g_files[i],g_files[i]);

			// i like to debug locally without having to load this!
			//if ( ! g_conf.m_isLive &&
			//     ! strcmp(g_files[i],"dict/unifiedDict") )
			//     continue;

			// get subdir in working dir
			//char subdir[512];
			//char *p = g_files[i];
			//char *last = NULL;
			//for ( ; *p ; p++ ) 
			//	if ( *p == '/' ) last = p;

			// try copying
			//char cmd[1024];
			//sprintf(cmd,"cp -p /home/mwells/gigablast/%s "
			//	"%s%s",g_files[i],g_hostdb.m_dir,g_files[i]);
			//log("db: trying to copy: \"%s\"",cmd);
			//system(cmd);

			needsFiles = true;
		}
			
	}

	if ( needsFiles ) {
	  log("db: use 'apt-get install -y netpbm' to install "
	      "pnmfiles");
	  return false;
	}

	// . check for tagdb files tagdb0.xml to tagdb50.xml
	// . MDW - i am phased these annoying files out 100%
	//for ( long i = 0 ; i <= 50 ; i++ ) {
	//	char tmp[100];
	//	sprintf ( tmp , "tagdb%li.xml" , i );
	//	File f;
	//	f.set ( dir , tmp );
	//	if ( ! f.doesExist() ) 
	//		return log("db: %s%s missing. Copy over from "
	//			   "titan:/gb/conf/%s",dir,tmp,tmp);
	//}


	if ( ! g_conf.m_isLive ) return true;

	// first check to make sure swap is off
	SafeBuf psb;
	if ( psb.fillFromFile("/proc/swaps") < 0 ) {
		log("gb: failed to read /proc/swaps");
		if ( ! g_errno ) g_errno = EBADENGINEER;
		return true;
	}

	/*
	File f;
	f.set ("/proc/swaps");
	long size = f.getFileSize() ;
	char *buf = (char *)mmalloc ( size+1, "S99" );
	if ( ! buf ) return false;
	if ( ! f.open ( O_RDONLY ) ) 
		return log("gb: failed to open %s",f.getFilename());
	if ( size != f.read ( buf , size , 0 ) ) 
		return log("gb: failed to read %s: %s",f.getFilename() ,
			   mstrerror(g_errno));
	buf[size] = '\0';
	*/
	char *buf = psb.getBufStart();
	if ( strstr ( buf,"dev" ) )
		return log("gb: can not start live gb with swap enabled.");

	// . make sure elvtune is being set right
	// . must be in /etc/rcS.d/S99local
	/*
	f.set ("/etc/rcS.d/S99local" );
	size = f.getFileSize() ;
	buf = (char *)mmalloc ( size+1, "S99" );
	if ( ! buf ) return false;
	if ( ! f.open ( O_RDONLY ) ) 
		return log("gb: failed to open %s",f.getFilename());
	if ( size != f.read ( buf , size , 0 ) ) 
		return log("gb: failed to read %s",f.getFilename() );
	buf[size]='\0';
	if ( ! strstr (buf,"\n/usr/sbin/elvtune -w 32 /dev/sda") ||
	     ! strstr (buf,"\n/usr/sbin/elvtune -w 32 /dev/sdb") ||
	     ! strstr (buf,"\n/usr/sbin/elvtune -w 32 /dev/sdc") ||
	     ! strstr (buf,"\n/usr/sbin/elvtune -w 32 /dev/sdd")   )
		// just note it now and do not exit since 2.6's elevator
		// tuning is totally different. NO! we are not using 2.6
		// cuz it sux...
		return log("gb: %s does not contain "
			   "/usr/sbin/elvtune -w 32 /dev/sd[a-d]" ,
			   f.getFilename());
	mfree ( buf , size+1, "S99" );
	*/

	// check kernel version
	FILE *fd;
	fd = fopen ( "/proc/version" , "r" );
	if ( ! fd ) {
		log("gb: could not open /proc/version to check kernel version:%s",
		    strerror(errno));
		return false;
	}
	// read in version
	char vbuf[4000];
	fgets ( vbuf , 3900 , fd );
	fclose ( fd );
	// compare it
	if ( strcmp ( vbuf , "Linux version 2.4.31-bigcore "
		      "(jolivares@voyager) (gcc version 2.95.4 20011002 "
		      "(Debian prerelease)) #2 SMP Fri Apr 14 12:48:46 "
		      "MST 2006\n") == 0 ) 
		return true;
	if ( strcmp ( vbuf , "Linux version 2.4.31-bigcore "
		      "(msabino@voyager) (gcc version 2.95.4 20011002 "
		      "(Debian prerelease)) #7 SMP Mon Aug 21 18:09:30 "
		      "MDT 2006\n")  == 0 )
		return true;
	// this one is for the dual and quad core machines i think
	if ( strcmp ( vbuf , "Linux version 2.4.34-e755 (jolivares@titan) "
		      "(gcc version 2.95.4 20011002 (Debian prerelease)) "
		      "#22 SMP Tue May 15 02:22:43 MDT 2007\n")==0 )
		return true;
	// temp hack test
	//if ( strcmp ( vbuf , "Linux version 2.6.30 (mwells@titan) (gcc "
	//	      "version 4.1.2 20061115 (prerelease) (Debian 4.1.1-"
	//	      "21)) #4 SMP Thu Jun 18 12:56:50 MST 2009\n")==0 )
	//	return true;
	// this is used for router0 and router1
	if ( g_hostdb.m_myHost->m_isProxy &&
	     strcmp ( vbuf , "Linux version 2.6.25.10 (mwells@titan) "
		      "(gcc version 4.1.2 20061115 (prerelease) "
		      "(Debian 4.1.1-21)) #9 SMP Sun Oct 12 15:23:40 "
		      "MST 2008\n")== 0)
		return true;
	log("gb: kernel version is not an approved version.");
	return false;

	return true;
}

static void powerMonitorWrapper ( int fd , void *state ) ;
static void fanSwitchCheckWrapper ( int fd , void *state ) ;
static void gotPowerWrapper ( void *state , TcpSocket *s ) ;
static void doneCmdWrapper        ( void *state ) ;
//static void  hdtempWrapper        ( int fd , void *state ) ;
static void  hdtempDoneWrapper    ( void *state , ThreadEntry *t ) ;
static void *hdtempStartWrapper_r ( void *state , ThreadEntry *t ) ;
static void heartbeatWrapper    ( int fd , void *state ) ;
//static void diskHeartbeatWrapper ( int fd , void *state ) ;
static void processSleepWrapper ( int fd , void *state ) ;

Process::Process ( ) {
	m_mode = NO_MODE;
	m_exiting = false;
	m_powerIsOn = true;
}

bool Process::init ( ) {
	// we do not know if the fans are turned off or on
	m_currentFanState = -1;
	m_threadOut = false;
	m_powerReqOut = false;
	m_powerIsOn = true;
	m_numRdbs = 0;
	m_suspendAutoSave = false;
	// . init the array of rdbs
	// . primary rdbs
	// . let's try to save tfndb first, that is the most important,
	//   followed by titledb perhaps...
	//m_rdbs[m_numRdbs++] = g_tfndb.getRdb       ();
	m_rdbs[m_numRdbs++] = g_titledb.getRdb     ();
	//m_rdbs[m_numRdbs++] = g_revdb.getRdb       ();
	//m_rdbs[m_numRdbs++] = g_sectiondb.getRdb   ();
	m_rdbs[m_numRdbs++] = g_posdb.getRdb     ();
	//m_rdbs[m_numRdbs++] = g_datedb.getRdb      ();
	m_rdbs[m_numRdbs++] = g_spiderdb.getRdb    ();
	m_rdbs[m_numRdbs++] = g_clusterdb.getRdb   (); 
	m_rdbs[m_numRdbs++] = g_tagdb.getRdb      ();
	//m_rdbs[m_numRdbs++] = g_catdb.getRdb       ();
	m_rdbs[m_numRdbs++] = g_statsdb.getRdb     ();
	m_rdbs[m_numRdbs++] = g_linkdb.getRdb      ();
	m_rdbs[m_numRdbs++] = g_cachedb.getRdb      ();
	m_rdbs[m_numRdbs++] = g_serpdb.getRdb      ();
	m_rdbs[m_numRdbs++] = g_monitordb.getRdb      ();
	//m_rdbs[m_numRdbs++] = g_placedb.getRdb     ();
	// save what urls we have been doled
	m_rdbs[m_numRdbs++] = g_doledb.getRdb      ();
	//m_rdbs[m_numRdbs++] = g_syncdb.getRdb      ();
	// secondary rdbs (excludes catdb)
	//m_rdbs[m_numRdbs++] = g_tfndb2.getRdb      ();
	m_rdbs[m_numRdbs++] = g_titledb2.getRdb    ();
	//m_rdbs[m_numRdbs++] = g_revdb2.getRdb      ();
	//m_rdbs[m_numRdbs++] = g_sectiondb2.getRdb  ();
	m_rdbs[m_numRdbs++] = g_posdb2.getRdb    ();
	//m_rdbs[m_numRdbs++] = g_datedb2.getRdb     ();
	m_rdbs[m_numRdbs++] = g_spiderdb2.getRdb   ();
	//m_rdbs[m_numRdbs++] = g_checksumdb2.getRdb ();
	m_rdbs[m_numRdbs++] = g_clusterdb2.getRdb  ();
	//m_rdbs[m_numRdbs++] = g_tagdb2.getRdb     ();
	//m_rdbs[m_numRdbs++] = g_statsdb2.getRdb    ();
	m_rdbs[m_numRdbs++] = g_linkdb2.getRdb     ();
	//m_rdbs[m_numRdbs++] = g_placedb2.getRdb    ();
	m_rdbs[m_numRdbs++] = g_tagdb2.getRdb      ();

	//call these back right before we shutdown the
	//httpserver.
	m_callbackState = NULL;
	m_callback      = NULL;

	// do not do an autosave right away
	m_lastSaveTime = 0;//gettimeofdayInMillisecondsLocal();
	// reset this
	m_sentShutdownNote = false;
	// this is used for shutting down as well
	m_blockersNeedSave = true;
	m_repairNeedsSave  = true;
	// count tries
	m_try = 0;
	// reset this timestamp
	m_firstShutdownTime = 0;
	// set the start time, local time
	m_processStartTime = gettimeofdayInMillisecondsLocal();
	// reset this
	m_lastHeartbeatApprox = 0;
	m_calledSave = false;

	// heartbeat check
	if ( ! g_loop.registerSleepCallback(100,NULL,heartbeatWrapper,0))
		return false;
	// we use SSDs now so comment this out
	//if ( !g_loop.registerSleepCallback(500,NULL,diskHeartbeatWrapper,0))
	//	return false;

	// get first snapshot of load average...
	//update_load_average(gettimeofdayInMillisecondsLocal());
	// . continually call this once per second
	// . once every half second now so that autosaves are closer together
	//   in time between all hosts
	if ( ! g_loop.registerSleepCallback(500,NULL,processSleepWrapper))
		return false;

	// . hard drive temperature
	// . now that we use intel ssds that do not support smart, ignore this
	//if ( ! g_loop.registerSleepCallback(10000,NULL,hdtempWrapper,0))
	//	return false;

	// power monitor, every 10 seconds
	if ( ! g_loop.registerSleepCallback(10000,NULL,powerMonitorWrapper,0))
		return false;

	// check temps to possible turn fans on/off every 30 seconds
	if ( !g_loop.registerSleepCallback(30000,NULL,fanSwitchCheckWrapper,0))
		return false;

	// -99 means unknown
	m_dataCtrTemp = -99;
	m_roofTemp    = -99;

	// success
	return true;
}

void powerMonitorWrapper ( int fd , void *state ) {
	if ( g_isYippy ) return;
	// are we in group #0
	bool checkPower = false;
	// get our host
	Host *me = g_hostdb.m_myHost;
	// if we are not host #0 and host #0 is dead, we check it
	if ( me->m_groupId == 0 && g_hostdb.isDead((long)0) ) 
		checkPower = true;
	// if we are host #0 we always check it
	if ( me->m_hostId == 0 ) checkPower = true;
	// proxy never checks power
	if ( me->m_isProxy ) checkPower = false;
	// if not checking, all done
	if ( ! checkPower ) return;
	// only if live
	if ( ! g_conf.m_isLive ) return;
	// skip if request out already
	if ( g_process.m_powerReqOut ) return;
	// the url
	char *url = "http://10.5.0.9/getData.htm";
	// download it
	//log(LOG_INFO,"powermo: getting %s",url);
	// for httpserver
	//Url u; u.set ( url , gbstrlen(url) );
	// mark the request as outstanding so we do not overlap it
	g_process.m_powerReqOut = true;
	// get it
	bool status = g_httpServer.
		getDoc ( url             , // url to download
			 0               , // ip
			 0               , // offset
			 -1              , // size
			 0               , // ifModifiedSince
			 NULL            , // state
			 gotPowerWrapper , // callback
			 30*1000         , // timeout
			 0               , // proxy ip
			 0               , // proxy port
			 1*1024*1024     , // maxLen
			 1*1024*1024     , // maxOtherLen
			 "Mozilla/4.0 "
			 "(compatible; MSIE 6.0; Windows 98; "
			 "Win 9x 4.90)"  ,
			 //false           , // respect download limit?
			 "HTTP/1.1"      );// fake 1.1 otherwise we get error!
	// wait for it
	if ( ! status ) return;
	// i guess it is back!
	g_process.m_powerReqOut = false;
	// call this to wrap things up
	g_process.gotPower ( NULL );
}

void gotPowerWrapper ( void *state , TcpSocket *s ) {
	g_process.gotPower ( s );
}

// . returns false if blocked, true otherwise
// . returns true and sets g_errno on error
bool Process::gotPower ( TcpSocket *s ) {

	// i guess it is back!
	g_process.m_powerReqOut = false;

	if ( ! s ) {
		log("powermo: got NULL socket");
		return true;
	}

	// point into buffer
	char *buf     ;
	long  bufSize ;

	// assume power is on
	long val = 0;
	HttpMime mime;
	char *content;
	long  contentLen;
	char *p;
	char *dataCtrTempStr;
	char *roofTempStr;

	if ( g_errno ) {
		log("powermo: had error getting power state: %s. assuming "
		    "power on.",
		    mstrerror(g_errno));
		//return true;
		// assume power went off
		//val = 1;
		goto skip;
	}
	// point into buffer
	buf     = s->m_readBuf;
	bufSize = s->m_readOffset;

	// note it
	//log(LOG_INFO,"powermo: got power reply");

	if ( ! buf ) {
		log(LOG_INFO,"powermo: got empty reply. assuming power on.");
		// return true;
		// assume power went off
		//val = 1;
		goto skip;
	}

	mime.set ( buf , bufSize , NULL );
	content    = buf     + mime.getMimeLen();
	contentLen = bufSize - mime.getMimeLen();
	content[contentLen]='\0';

	// get the state of the power!
	p = strstr ( content ,"\"power\",status:" );
	// panic?
	if ( ! p ) {
		log("powermo: could not parse out power from room alert. "
		    "assuming power on. "
		    "content = %s",content);
		//return true;
		// assume power went off
		//val = 1;
		goto skip;
	}
 
	// . get the value
	// . val is 0 if the power is ON!!!!
	// . val is non-zero if the power is OFF!!!
	val = atoi ( p + 15 );
	// random values for testing!!
	//val = rand()%2;


	// 
	// . now get the temperature in the data ctr and the roof
	// . log it every hour i guess for shits and giggles
	// . if the roof temp is less than the data ctr temp then
	//   we want to keep the fans on, otherwise we need to send an
	//   http request to the power strip control to turn the fans off
	//
	dataCtrTempStr = strstr( content, "\"Exit Temp\",tempf:\"" );
	if ( ! dataCtrTempStr ) {
		log("powermo: could not parse our data ctr temp from "
		    "room alert.");
		goto skip;
	}
	m_dataCtrTemp = atof ( dataCtrTempStr+19+4 );


	roofTempStr = strstr( content, "\"Roof Temp\",tempf:\"" );
	if ( ! dataCtrTempStr ) {
		log("powermo: could not parse out roof temp from "
		    "room alert.");
		goto skip;
	}
	m_roofTemp = atof ( dataCtrTempStr+19);



 skip:

	// 0 means the alert is not triggered and power is on
	if ( val == 0 && m_powerIsOn == true ) {
		//log("powermo: power is still ON.");
		return true;
	}
	// if it is off and was off before, don't do anything
	if ( val && m_powerIsOn == false ) {
		log("powermo: power is still OFF.");
		return true;
	}

	char *up = NULL;

	// if it was off before, tell everyone it is back on
	if ( val == 0 && m_powerIsOn == false ) {
		log("powermo: power is back ON!");
		up = "/master?haspower=1&username=msg28&cast=0";
		// update ourselves to prevent sending these multiple times
		//m_powerIsOn = true;
	}
	else if ( val && m_powerIsOn == true ) {
		log("powermo: power is OFF!");
		up = "/master?haspower=0&username=msg28&cast=0";
		// . update ourselves to prevent sending these multiple times
		// . no, we need to make sure to save in Parms.cpp::
		//   CmdPower
		//m_powerIsOn = false;
	}

	// how did this happen?
	if ( m_powerReqOut ) return true;

	// the request url
	//Url ru; ru.set ( up , gbstrlen(up) );
	// set the http reqeust
	if ( ! m_r.set ( up ) ) {
		log("powermo: got httpreqeust set error: %s",
		    mstrerror(g_errno));
		return true;
	}

	// we are out again...
	g_process.m_powerReqOut = true;

	log("powermo: sending notice to all hosts.");

	// . turn off spiders
	// . also show that power is off now!
	if ( ! m_msg28.massConfig ( m_r.getRequest() ,
				    NULL           , // state
				    doneCmdWrapper ) )
		// return false if this blocked
		return false;
	// . hmmm.. it did not block
	// . this does not block either
	doneCmdWrapper ( NULL );
	return true;
}

void doneCmdWrapper ( void *state ) {
	// we are back
	g_process.m_powerReqOut = false;
	// note it
	log("powermo: DONE sending notice to all hosts.");
}

void hdtempWrapper ( int fd , void *state ) {
	// reset this... why?
	g_errno = 0;
	// do not get if already getting
	if ( g_process.m_threadOut ) return;
	// skip if exiting
	if ( g_process.m_mode == EXIT_MODE ) return;
	// current local time
	long now = getTime();
	// or if haven't waited long enough
	if ( now < s_nextTime ) return;
	// set it
	g_process.m_threadOut = true;
	// . call thread to call popen
	// . callThread returns true on success, in which case we block
	if ( g_threads.call ( FILTER_THREAD        ,
			      MAX_NICENESS         ,
			      NULL                 , // this
			      hdtempDoneWrapper    ,
			      hdtempStartWrapper_r ) ) return;
	// back
	g_process.m_threadOut = false;
	// . call it directly
	// . only mention once to avoid log spam
	static bool s_first = true;
	if ( s_first ) {
		s_first = false;
		log("build: Could not spawn thread for call to get hd temps. "
		    "Ignoring hd temps. Only logging once.");
	}
	// MDW: comment these two guys out to avoid calling it for now
	// get the data
	//hdtempStartWrapper_r ( false , NULL ); // am thread?
	// and finish it off
	//hdtempDoneWrapper ( NULL , NULL );
}

// come back here
void hdtempDoneWrapper ( void *state , ThreadEntry *t ) {
	// we are back
	g_process.m_threadOut = false;
	// current local time
	long now = getTime();
	// if we had an error, do not schedule again for an hour
	//if ( s_flag ) s_nextTime = now + 3600;
	// reset it
	//s_flag = 0;
	// send email alert if too hot
	Host *h = g_hostdb.m_myHost;
	// get max temp
	long max = 0;
	for ( long i = 0 ; i < 4 ; i++ ) {
		short t = h->m_hdtemps[i];
		if ( t > max ) max = t;
	}
	// . leave if ok
	// . the seagates tend to have a max CASE TEMP of 69 C
	// . it says the operating temps are 0 to 60 though, so
	//   i am assuming that is ambient?
	// . but this temp is probably the case temp that we are measuring
	if ( max <= g_conf.m_maxHardDriveTemp ) return;
	// leave if we already sent and alert within 5 mins
	static long s_lasttime = 0;
	if ( now - s_lasttime < 5*60 ) return;
	// prepare msg to send
	char msgbuf[1024];
	Host *h0 = g_hostdb.getHost ( 0 );
	snprintf(msgbuf, 1024,
		 "hostid %li has overheated HD at %li C "
		 "cluster=%s (%s). Disabling spiders.",
		 h->m_hostId,
		 (long)max,
		 g_conf.m_clusterName,
		 iptoa(h0->m_ip));
	// send it, force it, so even if email alerts off, it sends it
	g_pingServer.sendEmail ( NULL   , // Host *h
				 msgbuf , // char *errmsg = NULL , 
				 true   , // bool sendToAdmin = true ,
				 false  , // bool oom = false ,
				 false  , // bool kernelErrors = false ,
				 false  , // bool parmChanged  = false ,
				 true   );// bool forceIt      = false );

	s_lasttime = now;
}

// . sets m_errno on error
// . taken from Msg16.cpp
void *hdtempStartWrapper_r ( void *state , ThreadEntry *t ) {

	static char *s_parm = "ata";
	// make a system call to /usr/sbin/hddtemp /dev/sda,b,c,d
	//char *cmd = 
	//	"/usr/sbin/hddtemp /dev/sda >  /tmp/hdtemp ;"
	//	"/usr/sbin/hddtemp /dev/sdb >> /tmp/hdtemp ;"
	//	"/usr/sbin/hddtemp /dev/sdc >> /tmp/hdtemp ;"
	//	"/usr/sbin/hddtemp /dev/sdd >> /tmp/hdtemp  ";
 retry:
	// linux 2.4 does not seem to like hddtemp
	char cmd[10048];
	char *path = g_hostdb.m_dir;
	//char *path = "/usr/sbin/";
	sprintf ( cmd ,
		  "%ssmartctl -Ad %s /dev/sda | grep Temp | awk '{print $10}' >  /tmp/hdtemp2;"
		  "%ssmartctl -Ad %s /dev/sdb | grep Temp | awk '{print $10}' >> /tmp/hdtemp2;"
		  "%ssmartctl -Ad %s /dev/sdc | grep Temp | awk '{print $10}' >> /tmp/hdtemp2;"
		  "%ssmartctl -Ad %s /dev/sdd | grep Temp | awk '{print $10}' >> /tmp/hdtemp2" ,
		  path,s_parm ,
		  path,s_parm ,
		  path,s_parm ,
		  path,s_parm );
	// the output
	char *out = "/tmp/hdtemp2";
	// timeout of 5 seconds
	//int err = my_system_r ( cmd , 5 );
	int err = system ( cmd );
	//logf(LOG_DEBUG,"proc: system \"%s\"",cmd);
	if ( err == 127 ) {
		//m_errno = EBADENGINEER;
		log("build: /bin/sh does not exist.");
		return NULL;
	}
	// this will happen if you don't upgrade glibc to 2.2.4-32 or above
	if ( err != 0 ) {
		//m_errno = EBADENGINEER;
		log("build: Call to system(\"%s\") had error.",cmd);
		//s_flag = 1;
		// wait an hour
		s_nextTime = getTime() + 3600;
		return NULL;
	}
	// read in temperatures from file
	int fd = open ( "/tmp/hdtemp2" , O_RDONLY );
	if ( fd < 0 ) {
		//m_errno = errno;
		log("build: Could not open %s for reading: %s.",
		    out,mstrerror(errno));
		return NULL;
	}
	char buf[2000];
	long r = read ( fd , buf , 2000 );
	// maybe try the marvell option?
	if ( r == 0 && s_parm[0]!='m' ) {
		log("gb: smartctl did not work. Trying marvell option.");
		s_parm = "marvell";
		goto retry;
	}
	else if ( r == 0 ) {
		log("gb: Please run apt-get install smartmontools to install "
		    "smartctl and then chown root:root %ssmartctl ; "
		    "chmod +s %ssmartctl. cmd=%s",path,path,cmd);
		// wait 5 mins
		s_nextTime = getTime() + 300;
	}
	// did we get an error
	if ( r < 0 ) {
		//m_errno = errno;
		log("build: Error reading %s: %s.",out,mstrerror(errno));
		close ( fd );
		return NULL;
	}
	// clean up shop
	close ( fd );
	// . typical file from hddtemp:
	//   /dev/sda: ST3400620AS: 39 C
	//   /dev/sdb: ST3400620AS: 39 C
	//   /dev/sdc: ST3400620AS: 39 C
	//   /dev/sdd: ST3400620AS: 39 C
	// . typical file from smartctl
	//   39\n37\n37\n37\n
	char *p = buf;
	// end
	char *pend = buf + gbstrlen(buf);
	// store the temps here
	short *temp = g_hostdb.m_myHost->m_hdtemps;
	// there are 4
	short *tempEnd = temp + 4;

	//
	// parse output from smartctl
	//
	while ( temp < tempEnd ) {
		// get temp
		*temp++ = atoi(p);
		// skip til after \n
		while ( p < pend && *p != '\n' ) p++;
		// skip \n
		p++;
		// done? strange.
		if ( p >= pend ) return NULL;
	}
	// done
	return NULL;

	//
	// parse output from hddtemp
	//

	// get all 4
	while ( temp < tempEnd ) {
		// skip till after 2nd colon
		while ( p < pend && *p!=':' ) p++;
		// skip over colon
		p++;
		// skip until we hit 2nd colon
		while ( p < pend && *p!=':' ) p++;
		// skip colon and space
		p += 2;
		// get temp
		*temp++ = atoi(p);
	}
	return NULL;
}

void Process::callHeartbeat () {
	heartbeatWrapper ( 0 , NULL );
}

void heartbeatWrapper ( int fd , void *state ) {
	static long long s_last = 0LL;
	static long long s_lastNumAlarms = 0LL;
	long long now = gettimeofdayInMilliseconds();
	if ( s_last == 0LL ) {
		s_last = now;
		s_lastNumAlarms = g_numAlarms;
		return;
	}
	// . log when we've gone 100+ ms over our scheduled beat
	// . this is a sign things are jammed up
	long long elapsed = now - s_last;
	if ( elapsed > 200 ) 
		// now we print the # of elapsed alarms. that way we will
		// know if the alarms were going off or not...
		log("db: missed heartbeat by %lli ms. Num elapsed alarms = "
		    "%li", elapsed-100,(long)(g_numAlarms - s_lastNumAlarms));
	s_last = now;
	s_lastNumAlarms = g_numAlarms;

	// save this time so the sig alarm handler can see how long
	// it has been since we've been called, so after 10000 ms it
	// can dump core and we can see what is holding things up
	g_process.m_lastHeartbeatApprox = g_nowApprox;
}


/*
void diskHeartbeatWrapper ( int fd , void *state ) {

	// skip this now that we use SSDs
	return;

	bool stuck = false;

	// do we have reads waiting?
	bool isWaiting = 
		( g_threads.m_threadQueues[DISK_THREAD].m_hiReturned <
		  g_threads.m_threadQueues[DISK_THREAD].m_hiLaunched ) ;

	// . must have been more than 1.5 secs since last read finished
	// . if the disk read queue is empty when we add a new read thread
	//   request in BigFile.cpp, we set g_diskRequestAdded to g_now
	if ( isWaiting && 
	     g_now - g_lastDiskReadCompleted >= 1500 &&
	     g_now - g_lastDiskReadStarted   >= 1500  )
		stuck = true;

	// return if not stuck
	if ( ! stuck ) {
		// if we just got unstuck, log that
		if ( g_diskIsStuck )
			log("gb: disk is now unstuck.");
		g_diskIsStuck = false;
		return;
	}

	// if first time, log that
	if ( ! g_diskIsStuck )
		log("gb: disk appears to be stuck.");

	// flag it so BigFile.cpp and File.cpp just return EDISKSTUCK and so
	// we do not kill all disk read threads again
	g_diskIsStuck = true;

	// now call the callback of all disk read threads that have niceness
	// 0 but set g_errno to EDISKSTUCK. when the actual read finally does
	// complete it should just basically stop...
	//
	// take this out now that we have solid states!!!!!!!!!!!!!
	//
	//g_threads.bailOnReads();
}
*/

void processSleepWrapper ( int fd , void *state ) {

        if ( g_process.m_mode == EXIT_MODE ) {g_process.shutdown2(); return; }
        if ( g_process.m_mode == SAVE_MODE ) {g_process.save2    (); return; }
        if ( g_process.m_mode == LOCK_MODE ) {g_process.save2    (); return; }
	if ( g_process.m_mode != NO_MODE   )                         return;

	// do not do autosave if no power
	if ( ! g_process.m_powerIsOn ) return;
	// autosave? override this if power is off, we need to save the data!
	//if (g_conf.m_autoSaveFrequency <= 0 && g_process.m_powerIsOn) return;
	if ( g_conf.m_autoSaveFrequency <= 0 ) return;
	// never if in read only mode
	if ( g_conf.m_readOnlyMode ) return;

	// skip autosave while sync in progress!
	if ( g_process.m_suspendAutoSave ) return;

	// need to have a clock unified with host #0
	if ( ! isClockInSync() ) return;
	// get time the day started
	long now = getTimeGlobal();
	// set this for the first time
	if ( g_process.m_lastSaveTime == 0 )
		g_process.m_lastSaveTime = now;
	
	//
	// we now try to align our autosaves with start of the day so that
	// all hosts autosave at the exact same time!! this should keep
	// performance somewhat consistent.
	//
	
	// get frequency in minutes
	long freq = (long)g_conf.m_autoSaveFrequency ;
	// convert into seconds
	freq *= 60;
	// how many seconds into the day has it been?
	long offset   = now % (24*3600);
	long dayStart = now - offset;
	// how many times should we have autosaved so far for this day?
	long autosaveCount = offset / freq;
	// convert to when it should have been last autosaved
	long nextLastSaveTime = (autosaveCount * freq) + dayStart;
	
	// if we already saved it for that time, bail
	if ( g_process.m_lastSaveTime >= nextLastSaveTime ) return;
	
	//long long now = gettimeofdayInMillisecondsLocal();
	// . get a snapshot of the load average...
	// . MDW: disable for now. not really used...
	//update_load_average(now);
	// convert from minutes in milliseconds
	//long long delta = (long long)g_conf.m_autoSaveFrequency * 60000LL;
	// if power is off make this every 30 seconds temporarily!
	//if ( ! g_process.m_powerIsOn ) delta = 30000;
	// return if we have not waited long enough
	//if ( now - g_process.m_lastSaveTime < delta ) return;
	// update
	g_process.m_lastSaveTime = nextLastSaveTime;//now;
	// save everything
	logf(LOG_INFO,"db: Autosaving.");
	g_process.save();
}

bool Process::save ( ) {
	// never if in read only mode
	if ( g_conf.m_readOnlyMode ) return true;
	// bail if doing something already
	if ( m_mode != 0 ) return true;
	// log it
	logf(LOG_INFO,"db: Entering lock mode for saving.");
	m_mode   = LOCK_MODE; // SAVE_MODE;
	m_urgent = false;
	return save2();
}

bool Process::shutdown ( bool urgent ,
			 void  *state,
			 void (*callback) (void *state )) {
	// bail if doing something already
	if ( m_mode != 0 ) {
		// if already in exit mode, just return
		if ( m_mode == EXIT_MODE )
			return true;
		// otherwise, log it!
		log("process: shutdown called, but mode is %li",
		    (long)m_mode);
		return true;
	}

	m_mode   = EXIT_MODE;
	m_urgent = urgent;

	// check memory buffers for overruns/underrunds to see if that
	// caused this core
	if ( urgent ) g_mem.printBreeches(false);

	if(!shutdown2()) {
		m_callbackState = state;
		m_callback = callback;
		return false;
	}
	return true;
}

// return false if blocked/waiting
bool Process::save2 ( ) {

	// MDW: why was this here? i commented it out. we need to do 
	//      quickpolls when autosaving for sure.
	//g_loop.disableTimer();

	// only the main process can call this
	if ( g_threads.amThread() ) return true;

	// . wait for any dump to complete
	// . when merging titldb, it sets Rdb::m_dump.m_isDumping to true
	//   because it is dumping the results of the merge to a file.
	//   occasionally it will initiate a dump of tfndb which will not be 
	//   possible because Rdb/RdbDump checks g_process.m_mode == SAVE_MODE,
	//   and do not allow dumps to begin if that is true! so we end up in 
	//   deadlock! the save can not complete 
	if ( isRdbDumping() ) return false;

	// ok, now nobody is dumping, etc. make it so no dumps can start.
	// Rdb.cpp/RdbDump.cpp check for this and will not dump if it is 
	// set to SAVE_MODE
	m_mode = SAVE_MODE;

	logf(LOG_INFO,"gb: Saving data to disk. Disabling writes.");

	// . disable adds/deletes on all rdb trees
	// . Msg1 requests will get ETRYAGAIN error replies
	// . this is instantaneous because all tree mods happen in this
	//   main process, not in a thread
	disableTreeWrites();

	bool useThreads = true;

	// . tell all rdbs to save trees
	// . will return true if no rdb tree needs a save
	if ( ! saveRdbTrees ( useThreads ) ) return false;

	// . save all rdb maps if they need it
	// . will return true if no rdb map needs a save
	// . save these last since maps can be auto-regenerated at startup
	if ( ! saveRdbMaps ( useThreads ) ) return false;

	// . save the conf files and caches. these block the cpu.
	// . save these first since more important than the stuff below
	// . no, to avoid saving multiple times, put this last since the
	//   stuff above may block and we have to re-call this function      
	if ( ! saveBlockingFiles1() ) return false;

	// save addsInProgress.dat etc. if power goes off. this should be the
	// one time we are called from power going off... since we do not
	// do autosave when the power is off. this just blocks and never
	// returns false, so call it with checking the return value.
	if ( ! g_process.m_powerIsOn ) saveBlockingFiles2() ;
	// for Test.cpp parser test we want to save the waitingtree.dat
	else if ( g_threads.m_disabled ) saveBlockingFiles2() ;

	// until all caches have saved, disable them
	g_cacheWritesEnabled = false;

	// . save caches
	// . returns true if NO cache needs to be saved
	//if ( ! saveRdbCaches ( useThreads ) ) return false;

	// bring them back
	g_cacheWritesEnabled = true;

	// reenable tree writes since saves were completed
	enableTreeWrites();

	log(LOG_INFO,"gb: Saved data to disk. Re-enabling Writes.");

	// update
	//m_lastSaveTime = gettimeofdayInMillisecondsLocal();

	// unlock
	m_mode = NO_MODE;

	return true;
}



// . return false if blocked/waiting
// . this is the SAVE BEFORE EXITING
bool Process::shutdown2 ( ) {
	g_loop.disableTimer();
	// only the main process can call this
	if ( g_threads.amThread() ) return true;

	if ( m_urgent )
		log(LOG_INFO,"gb: Shutting down urgently. Try #%li.",m_try++);
	else
		log(LOG_INFO,"gb: Shutting down. Try #%li.",m_try++);

	// turn off statsdb so it does not try to add records for these writes
	g_statsdb.m_disabled = true;

	// assume we will use threads
	bool useThreads = true;

	// if urgent do not allow any further threads to be spawned unless
	// they were already queued
	if ( m_urgent ) {
		// do not use thread spawning
		useThreads = false;
		// turn off all threads just in case
		if ( ! useThreads ) g_threads.disableThreads();
	}


	// disable all spidering
	// we can exit while spiders are in the queue because
	// if they are in the middle of being added they will be
	// saved by spider restore
	// wait for all spiders to clear
	g_conf.m_spideringEnabled = false;
	//g_conf.m_injectionEnabled = false;

	// . suspend all merges
	g_merge.suspendMerge () ;
	g_merge2.suspendMerge() ;
	// make sure they are in a saveable state. we need to make sure
	// they have dumped out the latest merged list and updated the 
	// appropriate RdbMap so we can save it below
	bool wait = false;
	if ( g_merge.m_isMerging  && ! g_merge.m_isReadyToSave  ) wait = true;
	if ( g_merge2.m_isMerging && ! g_merge2.m_isReadyToSave ) wait = true;
	// wait for any dump to complete
	if ( isRdbDumping() ) wait = true;
	// . wait for the merge or dump to complete
	// . but NOT if urgent...
	if ( wait && ! m_urgent ) return false;

	// . disable adds/deletes on all rdb trees
	// . Msg1 requests will get ECLOSING error msgs
	// . this is instantaneous because all tree mods happen in this
	//   main process, not in a thread
	disableTreeWrites();

	// . tell all rdbs to save trees
	// . will return true if no rdb tree needs a save
	if ( ! saveRdbTrees ( useThreads ) ) 
		if ( ! m_urgent ) return false;

	// save this right after the trees in case we core
	// in saveRdbMaps() again due to the core we are
	// handling now corrupting memory
	if ( m_repairNeedsSave ) {
		m_repairNeedsSave = false;
		g_repair.save();
	}

	// . save all rdb maps if they need it
	// . will return true if no rdb map needs a save
	if ( ! saveRdbMaps ( useThreads ) ) 
		if ( ! m_urgent ) return false;

	long long now = gettimeofdayInMillisecondsLocal();
	if ( m_firstShutdownTime == 0 ) m_firstShutdownTime = now;

	// these udp servers will not read in new requests or allow
	// new requests to be sent. they will timeout any outstanding
	// UdpSlots, and when empty they will return true here. they will
	// close their m_sock and set it to -1 which should force their
	// thread to exit.
	// if not urgent, they will wait for a while for the 
	// sockets/slots to clear up.
	// however, if 5 seconds or more have elapsed then force it
	bool udpUrgent = m_urgent;
	if ( now - m_firstShutdownTime >= 3000 ) udpUrgent = true;

	if ( ! g_dns.m_udpServer.shutdown ( udpUrgent ) )
		if ( ! udpUrgent ) return false;

	// . send notes to all the hosts in the network telling them we're
	//   shutting down
	// . this returns false if it blocks
	// . we don't care if it blocks or not
	// . don't bother asking the hosts to send an email alert for us
	//   since we're going down gracefully by letting everyone know
	// . don't send this unless we are very sure we can shutdown NOW
	// . i.e. no blocking after this call!
	if ( ! m_sentShutdownNote && ! m_urgent ) {
		log(LOG_INFO,"gb: Broadcasting shutdown notice.");
		m_sentShutdownNote = true;
		g_pingServer.broadcastShutdownNotes ( false , //sendEmailAlert?
						      NULL  , 
						      NULL  );
	}
	//broadcastShutdownNotes uses g_udpServer so we do this last.
	if ( ! g_udpServer.shutdown ( udpUrgent ) )
		if ( ! udpUrgent ) return false;


	g_profiler.stopRealTimeProfiler();
	g_profiler.cleanup();

	// save the conf files and caches. these block the cpu.
	if ( m_blockersNeedSave ) {
		m_blockersNeedSave = false;
		if (!g_conf.m_readOnlyMode)
			logf(LOG_INFO,"gb: Saving miscellaneous data files.");
		saveBlockingFiles1() ;
		saveBlockingFiles2() ;
	}

	// . save all rdb caches if they need it
	// . do this AFTER udp server is shut down so cache should not
	//   be accessed any more
	// . will return true if no rdb cache needs a save
	//if ( ! saveRdbCaches ( useThreads ) ) return false;

	// always diable threads at this point so g_threads.call() will
	// always return false and we do not queue any new threads for
	// spawning
	g_threads.disableThreads();

	// urgent means we need to dump core, SEGV or something
	if ( m_urgent ) {
		// log it
		log("gb: Dumping core after saving.");
		// at least destroy the page caches that have shared memory
		// because they seem to not clean it up
		resetPageCaches();
		// . force an abnormal termination which will cause a core dump
		// . do not dump core on SIGHUP signals any more though
		abort();
		// keep compiler happy
		return true;
	}



	// cleanup threads, this also launches them too
	g_threads.timedCleanUp(0x7fffffff,MAX_NICENESS);

	// wait for all threads to complete...
	long n = g_threads.getNumThreadsOutOrQueued() ;
	//if ( n > 0 )
	//	return log(LOG_INFO,
	//		   "gb: Waiting for %li threads to complete.",n);

	log(LOG_INFO,"gb: Has %li threads out.",n);


	//ok, resetAll will close httpServer's socket so now is the time to 
	//call the callback.
	if(m_callbackState) (*m_callback)(m_callbackState);

	// tell Mutlicast::reset() not to destroy all the slots! that cores!
	m_exiting = true;

	// let everyone free their mem
	resetAll();

	// show what mem was not freed
	g_mem.printMem();

	// kill any outstanding hd temp thread?
	if ( g_process.m_threadOut ) 
		log(LOG_INFO,"gb: still has hdtemp thread");

	// exit abruptly
	exit(0);

	// keep compiler happy
	return true;
}

void Process::disableTreeWrites ( ) {
	// loop over all Rdbs
	for ( long i = 0 ; i < m_numRdbs ; i++ ) {
		Rdb *rdb = m_rdbs[i];
		rdb->disableWrites();
	}
	// disable all spider trees and tables
	for ( long i = 0 ; i < g_spiderCache.m_numSpiderColls ; i++ ) {
		SpiderColl *sc = g_spiderCache.m_spiderColls[i];
		if ( ! sc ) continue;
		sc->m_waitingTree .disableWrites();
		sc->m_waitingTable.disableWrites();
		sc->m_doleIpTable .disableWrites();
	}
	
}

void Process::enableTreeWrites ( ) {
	// loop over all Rdbs
	for ( long i = 0 ; i < m_numRdbs ; i++ ) {
		Rdb *rdb = m_rdbs[i];
		rdb->enableWrites();
	}
	// enable all waiting trees
	for ( long i = 0 ; i < g_spiderCache.m_numSpiderColls ; i++ ) {
		SpiderColl *sc = g_spiderCache.m_spiderColls[i];
		if ( ! sc ) continue;
		sc->m_waitingTree .enableWrites();
		sc->m_waitingTable.enableWrites();
		sc->m_doleIpTable .enableWrites();
	}
}

// . returns false if blocked, true otherwise
// . calls callback when done saving
bool Process::isRdbDumping ( ) {
	// loop over all Rdbs and save them
	for ( long i = 0 ; i < m_numRdbs ; i++ ) {
		Rdb *rdb = m_rdbs[i];
		if ( rdb->m_dump.m_isDumping ) return true;
	}
	return false;
}

bool Process::isRdbMerging ( ) {
	// loop over all Rdbs and save them
	for ( long i = 0 ; i < m_numRdbs ; i++ ) {
		Rdb *rdb = m_rdbs[i];
		if ( rdb->isMerging() ) return true;
	}
	return false;
}

// . returns false if blocked, true otherwise
// . calls callback when done saving
bool Process::saveRdbTrees ( bool useThread ) {
	// never if in read only mode
	if ( g_conf.m_readOnlyMode ) return true;
	// turn off statsdb until everyone is done
	//g_statsdb.m_disabled = true;
	// loop over all Rdbs and save them
	for ( long i = 0 ; ! m_calledSave && i < m_numRdbs ; i++ ) {
		Rdb *rdb = m_rdbs[i];
		rdb->saveTree ( useThread );
	}

	// . save waitingtrees for each collection, blocks.
	// . can we make this non-blocking?
	// . true = "usethreads"
	// . all writes have been disabled, so should be cleanly saved
	// . if this did not block that means it does not need any saving
	// . this just launched all the write threads for the trees/tables
	//   that need to be saved. it sets m_isSaving once they are all 
	//   launched.
	// . and sets m_isSaving=false on SpiderCache::doneSaving when they
	//   are all done.
	g_spiderCache.save ( useThread );

	// do not re-save the stuff we just did this round
	m_calledSave = true;
	// quickly re-enable if statsdb tree does not need save any more
	//if ( ! g_statsdb.m_rdb.needsSave() ) g_statsdb.m_disabled = false;
	// check if any need to finish saving
	for ( long i = 0 ; i < m_numRdbs ; i++ ) {
		Rdb *rdb = m_rdbs[i];
		if ( rdb->needsSave ( ) ) return false;
	}

	// . check spider cache files (doleiptable waitingtree etc.)
	// . this should return true if it still has some files that haven't
	//   saved to disk yet... so if it returns true we return false 
	//   indicating that we are still waiting!
	if ( g_spiderCache.needsSave () ) return false;

	// reset for next call
	m_calledSave = false;
	// everyone is done saving
	return true;
}

// . returns false if blocked, true otherwise
// . calls callback when done saving
bool Process::saveRdbMaps ( bool useThread ) {
	// never if in read only mode
	if ( g_conf.m_readOnlyMode ) return true;
	useThread = false;
	// loop over all Rdbs and save them
	for ( long i = 0 ; i < m_numRdbs ; i++ ) {
		Rdb *rdb = m_rdbs[i];
		rdb->saveMaps ( useThread );
	}
	// everyone is done saving
	return true;
}

// . returns false if blocked, true otherwise
// . calls callback when done saving
/*
bool Process::saveRdbCaches ( bool useThread ) {
	// never if in read only mode
	if ( g_conf.m_readOnlyMode ) return true;
	//useThread = false;
	// loop over all Rdbs and save them
	for ( long i = 0 ; i < m_numRdbs ; i++ ) {
		Rdb *rdb = m_rdbs[i];
		// . returns true if cache does not need save
		// . returns false if blocked and is saving
		// . returns true if useThreads is false
		// . we return false if it blocks
		if ( ! rdb->saveCache ( useThread ) ) return false;
	}
	// everyone is done saving
	return true;
}
*/

bool Process::saveBlockingFiles1 ( ) {
	// never if in read only mode
	if ( g_conf.m_readOnlyMode ) return true;

	// save user accounting files. 3 of them.
	if ( g_hostdb.m_myHost->m_isProxy )
		g_proxy.saveUserBufs();

	// save the Conf file now
	g_conf.save();
	// save the conf files
        g_collectiondb.save();
	// . save repair state
	// . this is repeated above too
	// . keep it here for auto-save
	g_repair.save();
	// save the login table
	g_users.save();

	// save the query log buffer if it was modified by the 
	// runSeoQueryLoop() in seo.cpp which updates its
	// QueryLogEntry::m_minTop50Score member and corresponding timestamp
	if ( g_qbufNeedSave ) {
		char fname[1024];
		sprintf(fname,"querylog.host%li.dat",g_hostdb.m_hostId);
		g_qbuf.saveToFile(g_hostdb.m_dir,fname);
		log("process: saving changes to %s",fname);
		g_qbufNeedSave = false;
	}
	
	// . save the add state from Msg4.cpp
	// . these are records in the middle of being added to rdbs across
	//   the cluster
	// . saves to "addsinprogress.saving" and moves to .saved
	// . eventually this may replace "spiderrestore.dat"
	if ( g_repair.isRepairActive() ) saveAddsInProgress ( "repair-" );
	else                             saveAddsInProgress ( NULL      );

	// . save the syncdb quicktree and insync.dat file, very important!!
	// . must do this LAST so we truly no if in sync or not!!
	//g_syncdb.save();

	// in fctypes.cpp. save the clock offset from host #0's clock so
	// our startup is fast again
	saveTimeAdjustment();

	return true;
}

#include "PageTurk.h"

bool Process::saveBlockingFiles2 ( ) {
	// never if in read only mode
	if ( g_conf.m_readOnlyMode ) return true;

	// the spider dup request cache
	//g_spiderCache.m_dupCache.save( false ); // use threads?

	// save waitingtrees for each collection, blocks.
	//if ( ! g_spiderCache.save() ) return false;

	// save what templates each turk has turked
	//g_templateTable.save( g_hostdb.m_dir , "turkedtemplates.dat" );

	// the robots.txt cache
        Msg13::getHttpCacheRobots()->save( false ); // use threads?

        // save our caches
        for ( long i = 0; i < MAX_GENERIC_CACHES; i++ ) {
                if ( g_genericCache[i].useDisk() )
			// do not use threads
			g_genericCache[i].save( false );
        }
	// save dead wait cache
	//if ( g_deadWaitCache.useDisk     () ) 
	//	g_deadWaitCache    .save ();
	//if ( g_forcedCache.useDisk       () ) 
	//	g_forcedCache      .save ( false ); // use threads?
	//if ( g_alreadyAddedCache.useDisk () ) 
	//	g_alreadyAddedCache.save ( false ); // use threads?
        // save dns caches
        RdbCache *c ;
	c = g_dns.getCache();
        if ( c->useDisk() ) c->save( false ); // use threads?
	// save quota cache
	//c = &g_qtable;
        //if ( c->useDisk() ) c->save( false ); // use threads?
	// save current spidering process, "spiderrestore.dat"
	//g_spiderLoop.saveCurrentSpidering();
	// save autoban stuff
	g_autoBan.save();
        // this one too
	//      g_classifier.save();
        //g_siteBonus.save();
	// save state for top docs
	//g_pageTopDocs.saveStateToDisk();
	
	// save the turk url cache, urls and user states
	//g_pageTurk.saveCache();

	return true;
}

void Process::resetAll ( ) {
	g_log             .reset();
	g_hostdb          .reset();
	g_hostdb2         .reset();
	g_spiderLoop      .reset();

	for ( long i = 0 ; i < m_numRdbs ; i++ ) {
		Rdb *rdb = m_rdbs[i];
		rdb->reset();
	}

	//g_catdb           .reset();
	g_collectiondb    .reset();
	g_categories1     .reset();
	g_categories2     .reset();
	//g_robotdb       .reset();
	g_dns             .reset();
	g_udpServer       .reset();
	//g_dnsServer       .reset();
	//g_udpServer2    .reset();
	g_httpServer      .reset();
	g_loop            .reset();
	g_speller         .reset();
	//g_thesaurus       .reset();
	g_spiderCache     .reset();
	g_threads         .reset();
	g_ucUpperMap      .reset();
	g_ucLowerMap      .reset();
	g_ucProps         .reset();
	g_ucCombiningClass.reset();
	g_ucScripts       .reset();
	g_profiler        .reset();
	g_langList        .reset();
	g_autoBan         .reset();
	//g_qtable          .reset();
	//g_pageTopDocs     .destruct();
	g_pageNetTest     .destructor();

	for ( long i = 0; i < MAX_GENERIC_CACHES; i++ )
		g_genericCache[i].reset();

	// reset disk page caches
	resetPageCaches();

	// termfreq cache in Posdb.cpp
	g_termFreqCache.reset();
	// in Msg0.cpp
	//g_termListCache.reset();
	// in msg5.cpp
	//g_waitingTable.reset();

	g_wiktionary.reset();

	s_clusterdbQuickCache.reset();
	s_hammerCache.reset();
	s_table32.reset();

	resetDecompTables();
	//resetCompositionTable();
	//resetMsg6();
	resetPageAddUrl();
	resetHttpMime();
	reset_iana_charset();
	resetAdultBit();
	resetDomains();
	resetEntities();
	resetQuery();
	resetStopWords();
	resetAbbrTable();
	resetUnicode();
	//resetMsg20Cache();
	//resetMsg12();
	//resetLoadAvg();

	// reset other caches
	//g_robotdb.m_rdbCache.reset();
	g_dns.reset();
	//g_alreadyAddedCache.reset();
	//g_forcedCache.reset();
	// Msg20.cpp's parser cache
	//resetMsg20Cache();
	g_spiderCache.reset();
	g_spiderLoop.reset();
	g_wiki.reset();
	// query log table
	//g_qt.reset();
	// query log buffer
	g_qbuf.reset();
	g_profiler.reset();
	g_testResultsTree.reset();
	g_users.m_ht.reset();
	g_users.m_loginTable.reset();
	resetAddressTables();
	resetMsg13Caches();
	resetStopWordTables();
	//resetSynonymTables();
	resetDateTables();
	resetTestIpTable();
}

void Process::resetPageCaches ( ) {
	log("gb: Resetting page caches.");
	g_posdb           .getDiskPageCache()->reset();
	//g_datedb          .getDiskPageCache()->reset();
	g_linkdb          .getDiskPageCache()->reset();
	g_titledb         .getDiskPageCache()->reset();
	//g_sectiondb       .getDiskPageCache()->reset();
	g_tagdb           .getDiskPageCache()->reset();
	g_spiderdb        .getDiskPageCache()->reset();
	//g_tfndb           .getDiskPageCache()->reset();
	//g_checksumdb      .getDiskPageCache()->reset();
	g_clusterdb       .getDiskPageCache()->reset();
	//g_catdb           .getDiskPageCache()->reset();
	//g_placedb         .getDiskPageCache()->reset();
	g_doledb          .getDiskPageCache()->reset();
	//g_statsdb	  .getDiskPageCache()->reset();
}

// ============================================================================
// load average shedding via /proc/loadavg and an async BigFile
typedef struct {
	char		buf[20];		// read buffer
	double		load_average;		// last parsed load avg.
	int64_t		time_req;		// time of last parse
	int64_t		time_parse;
	bool		waiting;		// waiting on async result?
	bool		closing;		// shutting down...
	BigFile		bigfile;
	FileState	filestate;
} loadavg_state;

static loadavg_state		s_st_lavg;
/*
static void loadavg_callback(loadavg_state* state) {
	if (state == NULL)
		return;
	if (s_st_lavg.closing)
		return;
	// MDW: stop doing it for now, it is not accurate
	state->load_average = 0.00;
	return;
	if (s_st_lavg.filestate.m_errno != 0) {
		// do not thrash!
		// leave time_req alone so next open will occur in 5 seconds...
		// do not deadlock!
		// set load_average=0 until file can be successfully re-read.
		s_st_lavg.load_average = 0.0;
		s_st_lavg.bigfile.close();
		s_st_lavg.bigfile.setNonBlocking();
		s_st_lavg.bigfile.open(O_RDONLY);
		log(LOG_INFO, "build: errno %ld reading /proc/loadavg",
			s_st_lavg.filestate.m_errno);
		s_st_lavg.filestate.m_errno = 0;
		return;
	}
	state->time_parse = gettimeofdayInMilliseconds();
	state->waiting = false;
	state->load_average = atof(state->buf);
	log(LOG_DEBUG, "build: loadavg currently: %.2f latency %lld ms",
		state->load_average, state->time_parse - state->time_req);
}
*/

static loadavg_state*		s_state_ptr	=	NULL;
/*
static void update_load_average(int64_t now) {
	// initialize loadavg collection...
	if (s_state_ptr == NULL) {
		s_st_lavg.load_average = 0.0;
		s_st_lavg.time_req = 0;
		s_st_lavg.time_parse = 0;
		s_st_lavg.waiting = false;
		s_st_lavg.closing = false;
		s_st_lavg.bigfile.set("/proc", "loadavg");
		s_st_lavg.bigfile.setNonBlocking();
		s_st_lavg.bigfile.open(O_RDONLY);
		s_state_ptr = &s_st_lavg;
	}
	if (s_st_lavg.closing)
		return;
	if (s_st_lavg.waiting)
		return;
	// the 2.4 kernel updates /proc/loadavg on a 5-second interval
	if (s_st_lavg.waiting == false && now - s_st_lavg.time_req < (5 * 1000))
		return;

	s_st_lavg.time_req = now;
	s_st_lavg.waiting = true;
	s_st_lavg.filestate.m_errno = 0;
	if (!s_st_lavg.bigfile.read(	s_st_lavg.buf,
					sizeof(s_st_lavg.buf),
					0,
					&s_st_lavg.filestate))
		return;
	// if we did not block (as is normal for _this_ file), then
	// call callback directly and update state struct.
	loadavg_callback(s_state_ptr);
	return;
}
*/

double Process::getLoadAvg() {
	return s_st_lavg.load_average;
}
void Process::resetLoadAvg() {
	if (s_state_ptr == NULL)
		return;
	s_st_lavg.closing = true;
	s_state_ptr = NULL;
	s_st_lavg.bigfile.close();
}
//
// ============================================================================


/*
/////////////////////////////////////
//
// event nightly stats process
//
//////////////////////////////////////

//
// copied from main.cpp dumpEvents() function
//

static long s_lastRunTime = 0;

void eventStatSleepWrapper ( void *state , int fd ) {

	// why even register it if not host #0?
	if ( g_hostdb.m_myHostId != 0 ) { char *xx=NULL;*xx=0; }
	// local time. we are on host #0
	long now = getTimeLocal();
	// wait at least one hour
	if ( now - s_lastRunTime  < 3600 ) return;
	// wait until midnight us time
	long tod = now % 86400;
	// or shortly after
	if ( tod > 1500 ) return;
	// ok, execute it
	s_lastRunTime = now;
	// send to everyhost
	for ( long i = 0 ;i < g_hostdb.m_numHosts ; i++ ) {
		Host *h = g_hostdb.getHost(i);
		// reset his stats
		h->m_eventStats.clear();
		// skip if dead
		if ( h->isDead() ) continue;
		g_udpServer.sendRequest ( 0xdd ,
					  NULL ,
					  gotStatReply ,
					  &h->m_eventStats );//store reply here
		s_numRequests++;
	}
	// wait for replies!
	s_numReplies = 0;
}

void gotStatReply ( UdpSlot *slot ) {

	s_numReplies++;
	// wait for all replies to come in
	if ( s_numReplies < s_numRequests ) return;

	// ok, tally up
	EventStats total;
	total.clear();
	
	for ( long i = 0 ; i < g_hostdb.m_numHosts ; i++ ) {
		Host *h = g_hostdb.getHost(i);
		EventStats *es = &h->m_eventStats;
		total.m_active += es->m_active;
	}

	SafeBuf sb;

	// email cruft
	sb.safePrintf("EHLO gigablast.com\r\n"
		      //"MAIL from:<eventguru@eventguru.com>\r\n"
		      "MAIL From:<mwells2@gigablast.com>\r\n"
		      "RCPT To:<%s>\r\n"
		      "DATA\r\n"
		      "From: mwells <mwells2@gigablast.com>\r\n"
		      "MIME-Version: 1.0\r\n"
		      "To: %s\r\n"
		      "Subject: Event Stats\r\n"
		      "Content-Type: text/html; charset=UTF-8; format=flowed\r\n"
		      "Content-Transfer-Encoding: 8bit\r\n"
		      // mime header must be separated from body by 
		      // an extra \r\n
		      "\r\n"
		      "\r\n"
		      );

	sb.safePrintf("total expired events %li\n\n", total.m_expired );
	sb.safePrintf("total active events %li\n\n", total.m_active );

	// print the stats now a
	fprintf(stdout,"expired %li\n",expiredCount);
	fprintf(stdout,"active %li\n",activeCount);
	fprintf(stdout,"expired+active %li\n",expiredCount+activeCount);
	fprintf(stdout,"activeresultset1 %li\n",activeResultSet1Count);
	fprintf(stdout,"activeexperimental %li\n",activeExperimentalCount);
	fprintf(stdout,"activeresultset1+activeexperimental %li\n",
		activeResultSet1Count+activeExperimentalCount);
	fprintf(stdout,"activefacebook %li\n",facebookCount);
	fprintf(stdout,"activebadgeocoder %li\n",badGeocoderCount);
	// by country
	fprintf(stdout,"active by country\n");
	for ( long i = 0 ;i < 256 ; i++ ) {
		if ( ! cctable[i] ) continue;
		char *cs = getCountryCode ( (uint8_t)i );
		if ( ! cs ) continue;
		fprintf(stdout,"%s %li\n",cs,cctable[i]);
	}

	sb.safePrintf("%li of %li hosts reporting.\n\n",
		      s_numReplies, g_hostdb.m_numHosts );

	// email that to mwells2@gigablast.com
	long ip = atoip ( "10.5.54.47" ); // gk37, our mail server
	if ( ! ts->sendMsg ( ip,
			     25, // smtp (send mail transfer protocol) port
			     sb.getBufStart(),
			     sb.length(),
			     sb.length(),
			     sb.length(),
			     NULL, // es,
			     NULL, // gotEmailReplyWrapper,
			     60*1000,
			     1000*1024,
			     1000*1024 ) )
		log("estats: sent event stats email to mwells2@gigablast.com");
	// we did not block, so update facebook rec with timestamps
	//gotEmailReply( es , NULL );
	// we did not block
	log("estats: tcp sendMsg did not block!");
}

// defined in XmlDoc.cpp:
bool isExpired ( EventDisplay *ed , long nowUTC , long niceness );

// defined in Address.cpp
uint8_t getCountryIdFromAddrStr ( char *addr );

// . host #0 call this around midnight on every host...
// . dd is the stat dump
// . returns the stats
void handleRequestdd ( UdpSlot *slot , long netnice ) {

	// set stats
	EventStats es;

 loop:
	// use msg5 to get the list, should ALWAYS block since no threads
	if ( ! msg5.getList ( RDB_TITLEDB   ,
			      coll          ,
			      &list         ,
			      startKey      ,
			      endKey        ,
			      minRecSizes   ,
			      true,//includeTree   ,
			      false         , // add to cache?
			      0             , // max cache age
			      0,//startFileNum  ,
			      -1,//numFiles      ,
			      NULL          , // state
			      NULL          , // callback
			      0             , // niceness
			      false         , // err correction?
			      NULL          , // cache key ptr
			      0             , // retry num
			      -1            , // maxRetries
			      true          , // compensate for merge
			      -1LL          , // sync point
			      &msg5b        )){
		log(LOG_LOGIC,"db: getList did not block.");
		return;
	}
	// all done if empty
	if ( list.isEmpty() ) goto done;

	// loop over entries in list
	for ( list.resetListPtr() ; ! list.isExhausted() ;
	      list.skipCurrentRecord() ) {
		key_t k       = list.getCurrentKey();
		char *rec     = list.getCurrentRec();
		long  recSize = list.getCurrentRecSize();
		long long docId       = g_titledb.getDocIdFromKey ( k );
		if ( k <= lastKey ) 
			log("key out of order. "
			    "lastKey.n1=%lx n0=%llx "
			    "currKey.n1=%lx n0=%llx ",
			    lastKey.n1,lastKey.n0,
			    k.n1,k.n0);
		lastKey = k;
		// print deletes
		if ( (k.n0 & 0x01) == 0) {
			fprintf(stdout,"n1=%08lx n0=%016llx docId=%012lli "
			       "(del)\n", 
			       k.n1 , k.n0 , docId );
			continue;
		}

		// . make this
		// . (there's a mem leak so just new each time!)
		XmlDoc *xd;
		try { xd = new (XmlDoc); }
		catch ( ... ) {
			fprintf(stdout,"could not alloc for xmldoc\n");
			exit(-1);
		}

		// uncompress the title rec
		if ( ! xd->set2 ( rec , recSize , coll ,NULL , 0 ) )
			continue;


		// now log each event we got that we hashed
		char *p    = xd->ptr_eventData;
		char *pend = xd->ptr_eventData + xd->size_eventData;
		// scan them
		for ( ; p < pend ; ) {
			// cast it
			EventDisplay *ed = (EventDisplay *)p;
			// skip this event display blob
			p += ed->m_totalSize;
			// ok, transform the offsets into ptrs
			ed->m_desc  = (EventDesc *)((long)ed->m_desc  +
						    xd->ptr_eventData);
			ed->m_addr  = (char *)((long)ed->m_addr  + 
					       xd->ptr_eventData);
			ed->m_int   = (long *)((long)ed->m_int   + 
					       xd->ptr_eventData);
			ed->m_normDate=(char *)((long)ed->m_normDate + 
						xd->ptr_eventData);
			// do not repeat!
			ed->m_eventFlags |= EV_DESERIALIZED;

			// compile into EventStats class
			addInEventStats ( es , &es, nowUTC ) ;
		}

		mdelete ( xd , sizeof(XmlDoc) , "mainxd" );
		delete xd;

	}
	startKey = *(key_t *)list.getLastKey();
	startKey += (unsigned long) 1;
	// watch out for wrap around
	if ( startKey >= *(key_t *)list.getLastKey() ) goto loop;

}


void addInEventStats ( EventDisplay *ed , EventStats *es , long nowUTC ) {

	// count expired
	if ( isExpired(ed,nowUTC,MAX_NICENESS)) {
		es->m_expired++;
		return;
	}

	es->m_active++;
	// count bad geocoder (lat=999.000|888.000)
	if ( ed->m_geocoderLat > 180.0 ||
	     ed->m_geocoderLon < -180.0 ) 
		es->m_badGeocoder++;
	// count resultset1 unexpired
	bool hasTitle = false;
	if ( ed->m_eventFlags & EV_HASTITLEWORDS) hasTitle = true;
	if ( ed->m_eventFlags & EV_HASTITLEBYVOTES) hasTitle = true;
	bool hasDate = false;
	if ( ed->m_eventFlags & EV_HASTIGHTDATE ) hasDate = true;
	if ( hasTitle && hasDate ) es->m_resultSet1++;
	else es->m_otherResultSet++;
	// facebook
	if ( ed->m_eventFlags & EV_FACEBOOK ) es->m_facebook++;
	// counts by country. if 'us' will be empty
	uint8_t crid = getCountryIdFromAddrStr(ed->m_addr);
	es->m_cctable[crid]++;
}

*/


static void gotFanReplyWrapper ( void *state , TcpSocket *s ) {
	g_process.gotFanReply ( s );
}

//
// FAN SWITCH CHECKER
//
void fanSwitchCheckWrapper ( int fd , void *state ) {
	g_process.checkFanSwitch ();
}

void Process::checkFanSwitch ( ) {

	// skip for now
	return;

	// are we in group #0
	bool check = false;
	// get our host
	Host *me = g_hostdb.m_myHost;
	// if we are not host #0 and host #0 is dead, we check it
	if ( me->m_groupId == 0 && g_hostdb.isDead((long)0) ) 
		check = true;
	// if we are host #0 we always check it
	if ( me->m_hostId == 0 ) check = true;
	// proxy never checks power
	if ( me->m_isProxy ) check = false;
	// if not checking, all done
	if ( ! check ) return;
	// only if live
	if ( ! g_conf.m_isLive ) return;
	// skip if request out already
	if ( m_fanReqOut ) return;
	// both must be legit
	if ( m_roofTemp    <= -99.0 ) return;
	if ( m_dataCtrTemp <= -99.0 ) return;

	// for shits and giggles log it every 10 minutes
	long now = getTimeLocal();
	static long s_lastLogTime = 0;
	if ( s_lastLogTime - now > 60*10 ) {
		s_lastLogTime = now;
		log("powermo: dataCtrTemp=%.1f roofTemp=%.1f",
		    m_dataCtrTemp, 
		    m_roofTemp );
	}

	// what is the desired state? assume fans on.
	m_desiredFanState = 1;
	// if roof is hotter then fans off! we don't want hotter air.
	if ( m_roofTemp > m_dataCtrTemp ) 
		m_desiredFanState = 0;
	// if matches, leave alone
	if ( m_currentFanState == m_desiredFanState ) return;

	// ok change! the url
	char *url ;
	// . the IP9258 power controller
	// . default ip=192.168.1.100
	// . default user=admin
	// . default pwd=12345678
	// . default mac=00:92:00:00:00:3D
	// . the instruction sheet says to run IPEDIT on the cd with your
	//   computer directly connected to the IP9258 via the eth port in 
	//   order to get the default ip address of it.
	// . i changed the ip to 10.5.0.8 since the roomalert is at 10.5.0.9
	// . turn all 4 ports on or off so we can plug the fans into two
	//   separate ports
	if ( m_desiredFanState ) 
		url = "http://10.5.0.8/tgi/iocontrol.tgi?"
			"pw1Name=&"
			"P60=On&"     // THIS IS WHAT CHANGES!
			"P60_TS=0&"   // timer seconds?
			"P60_TC=Off&" // timer control?
			"pw2Name=&"
			"P61=On&"
			"P61_TS=0&"
			"P61_TC=Off&"
			"pw3Name=&"
			"P62=On&"
			"P62_TS=0&"
			"P62_TC=Off&"
			"pw4Name=&"
			"P63=On&"
			"P63_TS=0&"
			"P63_TC=Off&"
			"Apply=Apply"
			;
	else 
		url = "http://10.5.0.8/tgi/iocontrol.tgi?"
			"pw1Name=&"
			"P60=Off&"    // THIS IS WHAT CHANGES!
			"P60_TS=0&"   // timer seconds?
			"P60_TC=Off&" // timer control?
			"pw2Name=&"
			"P61=Off&"
			"P61_TS=0&"
			"P61_TC=Off&"
			"pw3Name=&"
			"P62=Off&"
			"P62_TS=0&"
			"P62_TC=Off&"
			"pw4Name=&"
			"P63=Off&"
			"P63_TS=0&"
			"P63_TC=Off&"
			"Apply=Apply"
			;

	// mark the request as outstanding so we do not overlap it
	m_fanReqOut = true;
	// . make a cookie with the login info
	// . on chrome open the console and click "Network" tab 
	//   to view the http network requests and replies
	char *cookie = "admin=12345678; Taifatech=yes";

	// get it
	bool status = g_httpServer.
		getDoc ( url             , // url to download
			 0               , // ip
			 0               , // offset
			 -1              , // size
			 0               , // ifModifiedSince
			 NULL            , // state
			 gotFanReplyWrapper , // callback
			 30*1000         , // timeout
			 0               , // proxy ip
			 0               , // proxy port
			 1*1024*1024     , // maxLen
			 1*1024*1024     , // maxOtherLen
			 "Mozilla/4.0 "
			 "(compatible; MSIE 6.0; Windows 98; "
			 "Win 9x 4.90)"  ,
			 //false           , // respect download limit?
			 "HTTP/1.1"      ,// fake 1.1 otherwise we get error!
			 true , // doPost? converts cgi str to post
			 cookie );
	// wait for it
	if ( ! status ) return;
	// i guess it is back!
	m_fanReqOut = false;
	// call this to wrap things up
	gotFanReply ( NULL );
}


// . returns false if blocked, true otherwise
// . returns true and sets g_errno on error
bool Process::gotFanReply ( TcpSocket *s ) {

	// i guess it is back!
	m_fanReqOut = false;

	if ( ! s ) {
		log("powermo: got NULL socket in fan reply");
		return true;
	}

	if ( g_errno ) {
		log("powermo: had error getting fan state: %s.",
		    mstrerror(g_errno));
		return true;
	}
	// point into buffer
	char *buf     = s->m_readBuf;
	long  bufSize = s->m_readOffset;

	if ( ! buf ) {
		log(LOG_INFO,"powermo: got empty fan state reply.");
		return true;
	}

	HttpMime mime;
	mime.set ( buf , bufSize , NULL );
	char *content    = buf     + mime.getMimeLen();
	long  contentLen = bufSize - mime.getMimeLen();
	content[contentLen]='\0';

	// get the state of the power!
	char *p = strstr ( content ,"\"power\",status:" );
	// panic?
	if ( ! p ) {
		log("powermo: could not parse out fan power state "
		    "from power strip. "
		    "content = %s",content);
		return true;
	}
 
	// . get the value
	// . val is 0 if the fan power off, 1 if on?
	long val = atoi ( p + 15 );

	m_currentFanState = val;

	if ( m_currentFanState == m_desiredFanState ) 
		log("powermo: desired fan state, %li, achieved",
		    m_currentFanState);
	else
		log("powermo: fan state is %li, but needs to be %li",
		    m_currentFanState, 
		    m_desiredFanState);

	return true;
}
	



// make sure ntpd is running, we can't afford to get our clock
// out of sync for credit card transactions
bool Process::checkNTPD ( ) {

	if ( ! g_conf.m_isLive ) return true;

	FILE *pd = popen("ps auxww | grep ntpd | grep -v grep","r");
	if ( ! pd ) {
		log("gb: failed to ps auxww ntpd");
		if ( ! g_errno ) g_errno = EBADENGINEER;
		return false;
	}
	char tmp[1024];
	char *ss = fgets ( tmp , 1000 , pd );
	if ( ! ss ) {
		log("gb: failed to ps auxww ntpd 2");
		if ( ! g_errno ) g_errno = EBADENGINEER;
		return false;
	}
	// must be there
	if ( ! strstr ( tmp,"ntpd") ) {
		log("gb: all proxies must have ntpd running! this "
		    "one does not!");
		if ( ! g_errno ) g_errno = EBADENGINEER;
		return false;
	}
	return true;
}

