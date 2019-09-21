#include "gb-include.h"

#include "Mem.h"
#include <sys/types.h>  // pid_t/getpid()
#include "Loop.h"
#include "Conf.h"
#include "Process.h"
#include "Threads.h" // getpidtid()

// a global class extern'd in Log.h
Log g_log;

#ifdef PTHREADS
#include <pthread.h>
// the thread lock
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

// . g_pbuf points to the parser buffer
// . we store text to be printed to html page in g_pbufPtr
// . we store normalized words/phrases into g_pterms
// . they are referenced by the TermTable's m_termPtrs[] buckets
// char      *g_pbuf     = NULL;
// char      *g_pbufPtr  = NULL;
// char      *g_pterms   = NULL;
// char      *g_ptermPtr = NULL;
// char      *g_pend     = NULL;
char      *g_dbuf          = NULL;
int32_t       g_dbufSize       = 0;

// main process id. pthread_t is 64 bit and pid_t is 32 bit on 64 bit oses
static pthread_t s_pid = (pthread_t)-1;

void Log::setPid ( ) {
	s_pid = getpidtid();
}

Log::Log () { 
	m_fd = -1; 
	m_filename = NULL; 
	m_hostname = NULL; 
	m_port = 777; 
	m_needsPrinting = false; 
	m_disabled = false;
	m_logTimestamps = false;
}

Log::~Log () { 
	//reset(); }
	if ( m_fd >= 0 ) ::close ( m_fd );
}	

// close file if it's open
void Log::reset ( ) {
	// comment this out otherwise we dont log the memleaks in Mem.cpp!!
	//if ( m_fd >= 0 ) ::close ( m_fd );
#ifdef DEBUG
	if (g_dbuf)
		mfree(g_dbuf,g_dbufSize,"Log: DebugBuffer");
	g_dbuf = NULL;
#endif
}

// for example, RENAME log000 to log000-2013_11_04-18:19:32
bool renameCurrentLogFile ( ) {
	File f;
	char tmp[16];
	sprintf(tmp,"log%03"INT32"",g_hostdb.m_hostId);
	f.set ( g_hostdb.m_dir , tmp );
	// make new filename like log000-2013_11_04-18:19:32
	time_t now = getTimeLocal();
	tm *tm1 = gmtime((const time_t *)&now);
	char tmp2[64];
	strftime(tmp2,64,"%Y_%m_%d-%T",tm1);
	SafeBuf newName;
	if ( ! newName.safePrintf ( "%slog%03"INT32"-%s",
				    g_hostdb.m_dir,
				    g_hostdb.m_hostId,
				    tmp2 ) ) {
		fprintf(stderr,"log rename failed\n");
		return false;
	}
	// rename log000 to log000-2013_11_04-18:19:32
	if ( f.doesExist() ) {
		//fprintf(stdout,"renaming file\n");
		f.rename ( newName.getBufStart() );
	}
	return true;
}


bool Log::init ( char *filename ) {
	// set the main process id
	//s_pid = getpidtid();
	setPid();
	// init these
	m_numErrors =  0;
	m_bufPtr    =  0;
	m_fd        = -1;
	m_disabled  = false;

#ifdef DEBUG
	g_dbufSize = 4096;
	g_dbuf = (char*)mmalloc(g_dbufSize,"Log: DebugBuffer");
	if (!g_dbuf) fprintf(stderr, "Unable to init debug buffer");
#endif
	//	m_hostname  = g_conf.m_hostname;
	//	m_port      = port;
	// is there a filename to log our errors to?
	m_filename = filename;
	if ( ! m_filename ) return true;

	// skip this for now
	//return true;

	//
	// RENAME log000 to log000-2013_11_04-18:19:32
	//
	if ( g_conf.m_runAsDaemon ) {
		// returns false on error
		if ( ! renameCurrentLogFile() ) return false;
	}

	// get size of current file. getFileSize() is defined in File.h.
	m_logFileSize = getFileSize ( m_filename );

	if ( strcmp(m_filename,"/dev/stderr") == 0 ) {
		m_fd = STDERR_FILENO; // 2; // stderr
		return true;
	}

	// open it for appending.
	// create with -rw-rw-r-- permissions if it's not there.
	m_fd = open ( m_filename , 
		      O_APPEND | O_CREAT | O_RDWR ,
		      getFileCreationFlags() );
		      // S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH );
	if ( m_fd >= 0 ) return true;
	// bitch to stderr and return false on error
	fprintf(stderr,"could not open log file %s for appending\n",
		m_filename);
	return false;
}
/*
static const char *getTypeString ( int32_t type ) ;

const char *getTypeString ( int32_t type ) {
	switch ( type ) {
	case LOG_INFO  : return "INFO ";
	case LOG_WARN  : return "WARN ";
	case LOG_LOGIC : return "LOGIC";
	case LOG_REMIND: return "REMND";
	case LOG_DEBUG : return "DEBUG";
	case LOG_TIMING: return "TIME ";
	case LOG_INIT  : return "INIT ";
	case LOG_LIMIT : return "LIMIT";
	default: return "     ";
	}
}
*/
#define MAX_LINE_LEN 20048

bool Log::shouldLog ( int32_t type , char *msg ) {
	// always log warnings
	if ( type == LOG_WARN ) return true;
	if ( type == LOG_INFO ) return g_conf.m_logInfo;
	// treat INIT as INFO
	//if ( type == LOG_INIT   && ! g_conf.m_logInfo      ) return true;
	if ( type == LOG_LIMIT  ) return g_conf.m_logLimits;
	if ( type == LOG_REMIND ) return g_conf.m_logReminders ;
	if ( type == LOG_TIMING ) {
		if ( msg[0] == 'a' && msg[2] == 'd' )
			return g_conf.m_logTimingAddurl;
		if ( msg[0] == 'a' && msg[2] == 'm' )
			return g_conf.m_logTimingAdmin;
		if ( msg[0] == 'b' ) return g_conf.m_logTimingBuild;
		if ( msg[0] == 'd' ) return g_conf.m_logTimingDb;
		if ( msg[0] == 'n' ) return g_conf.m_logTimingNet;
		if ( msg[0] == 'q' ) return g_conf.m_logTimingQuery;
		if ( msg[0] == 's' ) return g_conf.m_logTimingSpcache;
		if ( msg[0] == 't' ) return g_conf.m_logTimingTopics;
		return false;
	}
	if ( type != LOG_DEBUG ) return true;
	if (msg[0]=='a'&&msg[2]=='d' ) return g_conf.m_logDebugAddurl ;
	if (msg[0]=='a'&&msg[2]=='m' ) return g_conf.m_logDebugAdmin  ;
	if (msg[0]=='b'&&msg[1]=='u' ) return g_conf.m_logDebugBuild  ;
	if (msg[0]=='d'&&msg[1]=='b' ) return g_conf.m_logDebugDb     ;
	if (msg[0]=='d'&&msg[1]=='i' ) return g_conf.m_logDebugDisk   ;
	if (msg[0]=='d'&&msg[1]=='n' ) return g_conf.m_logDebugDns    ;
	if (msg[0]=='d'&&msg[1]=='o' ) return g_conf.m_logDebugDownloads;
	if (msg[0]=='h'&&msg[1]=='t' ) return g_conf.m_logDebugHttp   ;
	if (msg[0]=='i'&&msg[1]=='m' ) return g_conf.m_logDebugImage  ;
	if (msg[0]=='l'&&msg[1]=='o' ) return g_conf.m_logDebugLoop   ;
	if (msg[0]=='l'&&msg[1]=='a' ) return g_conf.m_logDebugLang   ;
	if (msg[0]=='m'&&msg[2]=='m' ) return g_conf.m_logDebugMem    ;
	if (msg[0]=='m'&&msg[2]=='r' ) return g_conf.m_logDebugMerge  ;
	if (msg[0]=='n'&&msg[1]=='e' ) return g_conf.m_logDebugNet    ;
	if (msg[0]=='p'&&msg[1]=='q' ) return g_conf.m_logDebugPQR    ;
	if (msg[0]=='q'&&msg[1]=='u'&&msg[2]=='e' ) 
		return g_conf.m_logDebugQuery  ;
	if (msg[0]=='q'&&msg[1]=='u'&&msg[2]=='o' ) 
		return g_conf.m_logDebugQuota  ;
	if (msg[0]=='r'&&msg[1]=='o' ) return g_conf.m_logDebugRobots ;
	//if (msg[0]=='s'&&msg[2]=='c' ) return g_conf.m_logDebugSpcache;
	if (msg[0]=='s'&&msg[1]=='e' ) return g_conf.m_logDebugSEO;
	if (msg[0]=='s'&&msg[2]=='e' ) return g_conf.m_logDebugSpeller;
	if (msg[0]=='s'&&msg[2]=='a' ) return g_conf.m_logDebugStats  ;
	if (msg[0]=='s'&&msg[1]=='u' ) return g_conf.m_logDebugSummary;
	if (msg[0]=='s'&&msg[2]=='i' ) return g_conf.m_logDebugSpider ;
	if (msg[0]=='t'&&msg[1]=='a' ) return g_conf.m_logDebugTagdb  ;
	if (msg[0]=='t'&&msg[1]=='c' ) return g_conf.m_logDebugTcp    ;
	if (msg[0]=='t'&&msg[1]=='h' ) return g_conf.m_logDebugThread ;
	if (msg[0]=='t'&&msg[1]=='i' ) return g_conf.m_logDebugTitle  ;
	if (msg[0]=='r'&&msg[1]=='e' ) return g_conf.m_logDebugRepair ;
	if (msg[0]=='u'&&msg[1]=='d' ) return g_conf.m_logDebugUdp    ;
	if (msg[0]=='u'&&msg[1]=='n' ) return g_conf.m_logDebugUnicode;
	if (msg[0]=='t'&&msg[1]=='o'&&msg[3]=='D' ) 
		return g_conf.m_logDebugTopDocs;
	if (msg[0]=='t'&&msg[1]=='o'&&msg[3]!='D' ) 
		return g_conf.m_logDebugTopics;
	if (msg[0]=='d'&&msg[1]=='a' ) return g_conf.m_logDebugDate;
	return true;
}

bool g_loggingEnabled = true;

// 1GB max log file size
#define MAXLOGFILESIZE 1000000000
// for testing:
//#define MAXLOGFILESIZE 3000

bool Log::logR ( int64_t now , int32_t type , char *msg , bool asterisk ,
		 bool forced ) {

	// filter if we should
	//if ( forced ) goto skipfilter;

	if ( ! g_loggingEnabled )
		return true;
	// return true if we should not log this
	if ( ! forced && ! shouldLog ( type , msg ) ) return true;
	// skipfilter:
	// can we log if we're a sig handler? don't take changes
	if ( g_inSigHandler ) 
		return logLater ( now , type , msg , NULL );
	//if ( g_inSigHandler ) return false;
	// get "msg"'s length
	int32_t msgLen = gbstrlen ( msg );

#ifdef PTHREADS
	// lock for threads
	pthread_mutex_lock ( &s_lock );
#endif

	// do a timestamp, too. use the time synced with host #0 because
	// it is easier to debug because all log timestamps are in sync.
	if ( now == 0 ) now = gettimeofdayInMillisecondsGlobalNoCore();

	// . skip all logging if power out, we do not want to screw things up
	// . allow logging for 10 seconds after power out though
	if ( ! g_process.m_powerIsOn && now - g_process.m_powerOffTime >10000){
#ifdef PTHREADS
		pthread_mutex_unlock ( &s_lock );
#endif
		return false;
	}

	//if ( now == 0 ) now  = g_nowApprox;
	// chop off any spaces at the end of the msg.
	while ( is_wspace_a ( msg [ msgLen - 1 ] ) && msgLen > 0 ) msgLen--;
	// get this pid
	pthread_t pid = getpidtid();
	// a tmp buffer
	char tt [ MAX_LINE_LEN ];
	char *p    = tt;
	//char *pend = tt + MAX_LINE_LEN;
	/*
	// print timestamp, hostid, type
	if ( g_hostdb.m_numHosts <= 999 ) 
		sprintf ( p , "%"UINT64" %03"INT32" %s ",
			  now , g_hostdb.m_hostId , getTypeString(type) );
	else if ( g_hostdb.m_numHosts <= 9999 ) 
		sprintf ( p , "%"UINT64" %04"INT32" %s ",
			  now , g_hostdb.m_hostId , getTypeString(type) );
	else if ( g_hostdb.m_numHosts <= 99999 ) 
		sprintf ( p , "%"UINT64" %05"INT32" %s ",
			  now , g_hostdb.m_hostId , getTypeString(type) );
	*/


	// print timestamp, hostid, type

	if ( m_logTimestamps ) {
		if ( g_hostdb.m_numHosts <= 999 ) 
			sprintf ( p , "%"UINT64" %03"INT32" ",
				  now , g_hostdb.m_hostId );
		else if ( g_hostdb.m_numHosts <= 9999 ) 
			sprintf ( p , "%"UINT64" %04"INT32" ",
				  now , g_hostdb.m_hostId );
		else if ( g_hostdb.m_numHosts <= 99999 ) 
			sprintf ( p , "%"UINT64" %05"INT32" ",
				  now , g_hostdb.m_hostId );
		p += gbstrlen ( p );
	}

	// msg resource
	char *x = msg;
	//int32_t cc = 7;
	// the first 7 bytes or up to the : must be ascii
	//while ( p < pend && *x && is_alnum_a(*x) ) { *p++ = *x++; cc--; }
	// space pad
	//while ( cc-- > 0 ) *p++ = ' ';
	// ignore the label for now...
	// MDW... no i like it
	//while ( p < pend && *x && is_alnum_a(*x) ) { x++; cc--; }
	// thread id if in "thread"
	if ( pid != s_pid && s_pid != (pthread_t)-1 ) {
		//sprintf ( p , "[%"INT32"] " , (int32_t)getpid() );
		sprintf ( p , "[%"UINT64"] " , (uint64_t)pid );
		p += gbstrlen ( p );
	}
	// then message itself
	int32_t avail = (MAX_LINE_LEN) - (p - tt) - 1;
	if ( msgLen > avail ) msgLen = avail;
	if ( *x == ':' ) x++;
	if ( *x == ' ' ) x++;
	strncpy ( p , x , avail );
	// capitalize for consistency. no, makes grepping log msgs harder.
	//if ( is_alpha_a(*p) ) *p = to_upper_a(*p);
	p += gbstrlen(p);
	// back up over spaces
	while ( p[-1] == ' ' ) p--;
	// end in period or ? or !
	//if ( p[-1] != '?' && p[-1] != '.' && p[-1] != '!' )
	//	*p++ = '.';
	*p ='\0';
	// the total length, not including the \0
	int32_t tlen = p - tt;

	// call sprintf, but first make sure we have room in m_buf and in
	// the arrays. who know how much room the sprintf is going to need???
	// NOTE: TODO: this is shaky -- fix it!
	if ( m_bufPtr + tlen  >= 1024 * 32 ||  m_numErrors  >= MAX_LOG_MSGS){
		// this sets m_bufPtr to 0
		if ( ! dumpLog ( ) ) {
			fprintf(stderr,"Log::log: could not dump to file!\n");
#ifdef PTHREADS
			pthread_mutex_unlock ( &s_lock );
#endif
			return false;
		}
	}
	// . filter out nasty chars from the message
	// . replace with ~'s
	char cs;
	char *ttp    = tt;
	char *ttpend = tt + tlen;
	for ( ; ttp < ttpend ; ttp += cs ) {
		cs = getUtf8CharSize ( ttp );
		if ( is_binary_utf8 ( ttp ) ) {
			for ( int32_t k = 0 ; k < cs ; k++ ) *ttp++ = '.';
			// careful not to skip the already skipped bytes
			cs = 0;
			continue;
		}
		// convert \n's and \r's to spaces
		if ( *ttp == '\n' ) *ttp = ' ';
		if ( *ttp == '\r' ) *ttp = ' ';
		if ( *ttp == '\t' ) *ttp = ' ';
	}

	// . if filesize would be too big then make a new log file
	// . should make a new m_fd
	if ( m_logFileSize + tlen+1 > MAXLOGFILESIZE && g_conf.m_runAsDaemon )
		makeNewLogFile();

	if ( m_fd >= 0 ) {
		write ( m_fd , tt , tlen );
		write ( m_fd , "\n", 1 );
		m_logFileSize += tlen + 1;
	}
	else {
		// print it out for now
		fprintf ( stderr, "%s\n", tt );
	}



	// set the stuff in the array
	m_errorMsg      [m_numErrors] = msg;
	m_errorMsgLen   [m_numErrors] = msgLen;
	m_errorTime     [m_numErrors] = now;
	m_errorType     [m_numErrors] = type;
	// increase the # of errors
	m_numErrors++;

#ifdef PTHREADS
	// unlock for threads
	pthread_mutex_unlock ( &s_lock );
#endif
	return false;
}

bool Log::makeNewLogFile ( ) {

	// prevent deadlock. don't log since we are in the middle of logging.
	// otherwise, safebuf, which is used when renaming files, might
	// call logR().
	g_loggingEnabled = false;
	// . rename old log file like log000 to log000-2013_11_04-18:19:32
	// . returns false on error
	bool status = renameCurrentLogFile();
	// re-enable logging since nothing below should call logR() indirectly
	g_loggingEnabled = true;
	if ( ! status ) return false;
	// close old fd
	if ( m_fd >= 0 ) ::close ( m_fd );
	// invalidate
	m_fd = -1;
	// reset
	m_logFileSize = 0;
	// open it for appending.
	// create with -rw-rw-r-- permissions if it's not there.
	m_fd = open ( m_filename , 
		      O_APPEND | O_CREAT | O_RDWR ,
		      getFileCreationFlags() );
		      // S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH );
	if ( m_fd >= 0 ) return true;
	// bitch to stderr and return false on error
	fprintf(stderr,"could not open new log file %s for appending\n",
		m_filename);
	return false;
}

// keep a special buf
static char  s_buf[1024*64];
static char *s_bufEnd   = s_buf + 1024*64;
static char *s_ptr      = s_buf;
static bool  s_overflow = false;
static char  s_problem  = '\0';

// . if we're in a sig handler come here
// . store:
// . 4 bytes = size of string space
// . X bytes = NULL terminated format string
// . X bytes = 0-3 bytes word-alignment padding
bool Log::logLater ( int64_t now, int32_t type, char *format, va_list ap ) {
	//return false;
	// we have to be in a sig handler
	//if ( ! g_inSigHandler ) 
	// fprintf(stderr,"Log::logLater: this should not have been called\n");
	// was it set before us?
	//bool queueSig = ! m_needsPrinting;
	// set the signal to print this out later
	m_needsPrinting = true;
	// save old s_ptr in case we have error
	char *start = s_ptr;
	// size of format string we must store
	int32_t flen = gbstrlen ( format ) + 1;
	// do we have room to store the stuff?
	if ( s_ptr + 4 + 4 + flen > s_bufEnd ) {
		s_overflow = true; 
		return false;
	}
	// first 4 bytes are the size of the string space, write later
	int32_t stringSizes = 0;
	s_ptr += 4;
	// the priorty is the 2nd 4 bytes
	memcpy_ass ( s_ptr , (char *)&type , 4 );
	s_ptr += 4;
	// store the format string first
	memcpy_ass ( s_ptr , format , flen );
	s_ptr += flen;
	// the type of each arg is given by format
	char *p = format;
	// point to the variable args data
	char *pap = (char *)ap;
	// loop looking for %s, %"INT32", etc.
 loop:
	while ( *p && *p != '%' ) p++;
	// bail if done
	if ( ! *p ) goto done;
	// skip the percent
	p++;
	// skip if back to back
	if ( *p == '%' ) { p++; goto loop; }
	// skip following numbers, those are part of format
	while ( *p && is_digit(*p) ) p++;
	// is it a int32_t, half or int? if so, leave as is.
	if ( *p == 'l' ) { 
		pap += 4; 
		// it could be a int64_t
		if ( (*p+1) == 'l' ) pap += 4;
		goto loop; 
	}
	if ( *p == 'h' ) { pap += 4; goto loop; }
	if ( *p == 'i' ) { pap += 4; goto loop; }
	if ( *p == 'c' ) { pap += 4; goto loop; }
	// . it it a string?
	// . if so store it on s_ptr
	if ( *p == 's' ) {
		char *s = *(char **)pap;
		int32_t  slen = gbstrlen(s) + 1;
		if ( s_ptr + slen >= s_bufEnd ) {
			s_ptr = start;
			s_overflow = true; 
			return false;
		}
		memcpy_ass ( s_ptr , s , slen );
		*(char **)pap = s_ptr; // replace old char ptr to use ours now
		s_ptr += slen;
		stringSizes += slen;
		pap += 4; // skip over a char ptr arg
		goto loop;
	}
	// panic if we don't know the type!!!
	s_ptr = start;
	s_problem = *p;
	return false;

	// save the args themselves and the time
 done:
	// how big were the strings we stored, if any
	memcpy_ass ( start , (char *)&stringSizes , 4 );
	// size of args we must store
	int32_t apsize = pap - (char *)ap;
	// bail if not enough room
	if ( s_ptr + 8 + 4 + 3 + apsize >= s_bufEnd ) {
		s_ptr = start;
		s_overflow = true;
		return false;
	}
	// store the time stamp in milliseconds
	memcpy_ass ( s_ptr , (char *)&now , 8 );
	s_ptr += 8;
	// store arg sizes
	memcpy_ass ( s_ptr , (char *)&apsize , 4 );
	s_ptr += 4;
	// dword align
	int32_t rem = ((PTRTYPE)s_ptr) % 4;
	if ( rem > 0 ) s_ptr +=  4 - rem;
	// store the args themselves
	memcpy_ass ( s_ptr , (char *)ap , apsize );
	s_ptr += apsize;
	// queue a signal if we need to
	//if ( ! queueSig ) return false;
	// queue the signal
	//sigval_t svt; 
	//svt.sival_int = 1;
	//if ( sigqueue ( s_pid, GB_SIGRTMIN + 1 + 0, svt ) < 0)
	//	g_loop.m_needToPoll = true;
	return false;
}

// once we're no longer in a sig handler this is called by Loop.cpp
// if g_log.needsPrinting() is true
void Log::printBuf ( ) {
	// not in sig handler
	if ( g_inSigHandler ) return;
	// was there a problem?
	if ( s_problem  ) fprintf(stderr,"Log::printBuf: had problem. '%c'\n",
				  s_problem);
	// or overflow?
	if ( s_overflow ) fprintf(stderr,"Log::printBuf: had overflow.\n");
	// point to the buf
	char *p    = s_buf;
	char *pend = s_ptr;
	// reset everything
	s_overflow      = false;
	s_problem       = '\0';
	m_needsPrinting = false;
	// bail if nothing to print, maybe first msg overflowed?
	if ( s_buf == s_ptr ) return;
	// we cannot be interrupted in here
	//bool flipped = false;
	//if ( g_interruptsOn ) {
	//	flipped = true;
	//	g_loop.interruptsOff();
	//}
	// reset buffer here
	s_ptr           = s_buf;
	// now print all log msgs we got while in a signal handler
 loop:
	// sanity check
	if ( p + 8 > pend ) {
		fprintf(stderr,"Had error in Log.cpp: breech of log buffer4.");
		s_ptr = s_buf;
		return;
	}
	// first 4 bytes are the size of the string arguments
	int32_t stringSizes;
	gbmemcpy ( (char *)&stringSizes , p , 4 );
	p += 4;
	// then the type of the msg
	int32_t type;
	gbmemcpy ( (char *)&type , p , 4 );
	p += 4;
	// then the format string
	char *format = p;
	// its length including the \0
	//int32_t flen = gbstrlen ( format ) + 1;
	int32_t flen = 0;
	char *q = format;
	while ( q < pend && *q ) q++;
	if ( q >= pend ) {
		fprintf(stderr,"Had error in Log.cpp: breech of log buffer3.");
		s_ptr = s_buf;
		return;
	}
	flen = q - p + 1;
	p += flen;
	// skip the string arguments now
	p += stringSizes;
	// sanity check
	if ( p + 8 + 4 > pend || p < s_buf ) {
		fprintf(stderr,"Had error in Log.cpp: breech of log buffer2.");
		s_ptr = s_buf;
		return;
	}
	// get time
	int64_t now ;
	gbmemcpy ( (char *)&now , p , 8 );
	p += 8;
	// get size of args
	int32_t apsize ;
	gbmemcpy ( (char *)&apsize , p , 4 );
	p += 4;
	// dword align
	int32_t rem = ((PTRTYPE)p) % 4;
	if ( rem > 0 ) p +=  4 - rem;
	// get va_list... needs to be word aligned!!
	va_list ap ;
	// MDW FIX ME
	//ap = (char *)(void*)p;
	p += apsize;
	// . sanity check
	// . i've seen this happen a lot lately since i started logging cancel
	//   acks perhaps?
	if ( p > pend || p < s_buf ) {
		fprintf(stderr,"Had error in Log.cpp: breech of log buffer.");
		s_ptr = s_buf;
		return;
	}
	// print msg into this buf
	char buf[1024*4];
	// print it into our buf now
	vsnprintf ( buf , 1024*4 , format , ap );
	// pass buf to g_log
	logR ( now , type , buf , true );
	// if not done loop back
	if ( p < pend ) goto loop;
	// turn 'em back on if we turned them off
	//if ( flipped ) g_loop.interruptsOn();
}

#include <ctype.h> // isascii()

// . IMPORTANT: should be called while the lock is on!
// . we just re-write to the file
bool Log::dumpLog ( ) {
	// . usually g_errno is set to something
	// . save it in case we set g_errno
	int32_t errnum = g_errno;
	// for now don't dump
	m_numErrors =  0;
	m_bufPtr    =  0;
	// just return true if no file open
	if ( m_fd < 0 ) return true;
	// for now just return true always
	return true;
	// . remove half the error from our memory buffer.
	// . log then shift the first half outta the picture... bye bye 
	// . we add one because its safe to assume we have 1 more msg than 
	//   we do..???? TODO
	// now shift over the old stuff in the arrays...
	// be sure to record it in the file if we can before we shift over it.
	for ( int i=0; i < m_numErrors ; i++ ) {
		// get time in seconds
		time_t t = m_errorTime[i] / 1000;
		// hack off ctime's appended \n
		//char *ct = ctime ( &t );
		char *ct = asctime(gmtime ( &t ));
		ct[gbstrlen(ct)-1] = '\0';
		char tmp[1024 * 2];
		snprintf ( tmp , 1024*2 , "%s(UTC):%s" , ct , m_errorMsg[i] );
		// TODO: add port #
		int32_t tmpLen = gbstrlen(tmp);
		// filter out garbage
		for ( int32_t j=0; j < tmpLen ; j++ )
			if ( !isascii(tmp[j]) ) tmp[j]='X';
		int32_t n = write ( m_fd , tmp , tmpLen ); 
		if ( n == tmpLen ) continue;
		fprintf(stderr,"Log::dumpLog: %s\n",mstrerror(g_errno));
		// reload the original g_errno
		g_errno = errnum;
		break;
	}
	// reset everything
	m_numErrors =  0;
	m_bufPtr    =  0;
	return true;
}

bool log ( int32_t type , char *formatString , ...) {
	if ( g_log.m_disabled ) return false;
	// do not log it if we should not
	if ( ! g_log.shouldLog ( type , formatString ) ) return false;
	// do not log it if we should not
	//if ( type == LOG_WARN && ! g_conf.m_logWarnings ) return false;
	// is it congestion?
	if ( g_errno == ENOSLOTS && ! g_conf.m_logNetCongestion ) return false;
	// this is the argument list (variable list)
	va_list   ap;
	// can we log if we're a sig handler? don't take changes
	// print msg into this buf
	char buf[1024*10];
	// copy the error into the buffer space
	va_start ( ap, formatString);
	// debug hack for testing
	if ( g_inSigHandler ) 
		return g_log.logLater ( g_now , type , formatString , ap);
	// print it into our buf now
	vsnprintf ( buf , 1024*10 , formatString , ap );
	va_end(ap);
	// pass buf to g_log
	g_log.logR ( 0 , type , buf , false );
	// always return false
	return false;
}

bool log ( char *formatString , ... ) {
	if ( g_log.m_disabled ) return false;
	// do not log it if we should not
	if ( ! g_log.shouldLog ( LOG_WARN , formatString ) ) return false;
	// is it congestion?
	if ( g_errno == ENOSLOTS && ! g_conf.m_logNetCongestion ) return false;
	// this is the argument list (variable list)
	va_list   ap;
	// can we log if we're a sig handler? don't take changes
	// print msg into this buf
	char buf[1024*10];
	// copy the error into the buffer space
	va_start ( ap, formatString);
	// debug hack for testing
	if ( g_inSigHandler ) 
		return g_log.logLater ( g_now , LOG_WARN , formatString , ap);
	// print it into our buf now
	vsnprintf ( buf , 1024*10 , formatString , ap );
	va_end(ap);
	// pass buf to g_log
	g_log.logR ( 0 , LOG_WARN , buf , false );
	// always return false
	return false;
}

bool logf ( int32_t type , char *formatString , ...) {
	if ( g_log.m_disabled ) return false;
	// do not log it if we should not
	//if ( type == LOG_WARN && ! g_conf.m_logWarnings ) return false;
	// is it congestion?
	if ( g_errno == ENOSLOTS && ! g_conf.m_logNetCongestion ) return false;
	// this is the argument list (variable list)
	va_list   ap;
	// can we log if we're a sig handler? don't take changes
	// print msg into this buf
	char buf[1024*10];
	// copy the error into the buffer space
	va_start ( ap, formatString);
	// debug hack for testing
	if ( g_inSigHandler ) 
		return g_log.logLater ( g_now , type , formatString , ap);
	// print it into our buf now
	vsnprintf ( buf , 1024*10 , formatString , ap );
	va_end(ap);
	// pass buf to g_log
	g_log.logR ( 0 , type , buf , false , true /*forced?*/ );
	// always return false
	return false;
}

