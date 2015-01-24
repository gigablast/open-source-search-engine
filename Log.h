// Matt Wells, copyright Feb 2001

// . a great way to record errors encountered during
// . we store the errorMsg, it's length, the type of message and time.
// . when our buf gets full we dump half the messages to the log file (if any)
// . netLogdb can send error msgs for you with it's sendError cmd
// . sendError ( UdpSlot *slot , char *errFormat , ...); (also logs it)

#ifndef _MYLOG_H_
#define _MYLOG_H_

// THE TYPES OF LOG MESSAGES
// logs information pertaining to more complicated procedures, like
// the merging and dumping of data for the "db" component, or what urls are 
// being spidered for the "build" component.
#define LOG_INFO     0x0001
// the default log message type. also logs slow performance.
#define LOG_WARN     0x0004
// programmer error. sanity check. always on.
#define LOG_LOGIC    0x0010  

// Reminders to fix the code. generally disabled.
#define LOG_REMIND   0x0020  
// for debugging. generally disabled.
#define LOG_DEBUG    0x0040  
// times various subroutines for debugging performance.
#define LOG_TIMING   0x0100

// initialization (and shutdown) information. also print routines. always on.
#define LOG_INIT     0x0400

// if a url or link gets truncated, uses this in the "build" context. (Url.cpp
// and Links.cpp)
// also used if a document not added due to quota breech. (Msg16.cpp)
// also used if too many nested tags to parse doc correctly (Xml.cpp)
// in the "query" context for serps too big to be cached. (Msg17.cpp)
#define LOG_LIMIT    0x2000  


// It is convenient to divide everything into components and allow the admin
// to toggle logging for various aspects, such as performance or timing
// messages, of these components:

// addurls related to adding urls
// admin   related to administrative things, sync file, collections
// build   related to indexing (high level)
// conf    configuration issues
// disk    disk reads and writes
// dns     dns networking
// http    http networking
// loop
// net     network later: multicast pingserver. sits atop udpserver.
// query   related to querying (high level)
// rdb     generic rdb things
// spcache related to determining what urls to spider next
// speller query spell checking
// thread  calling threads
// topics  related topics
// udp     udp networking

// example log:
//456456454 0 INIT         Gigablast Version 1.234
//454544444 0 INIT  thread Allocated 435333 bytes for thread stacks.
//123456789 0 WARN  mem    Failed to alloc 360000 bytes. 
//123456789 0 WARN  query  Failed to intersect lists. Out of memory.
//123456789 0 WARN  query  Too many words. Query truncated.
//234234324 0 REQST http   1.2.3.4 GET /index.html User-Agent
//234234324 0 REPLY http   1.2.3.4 sent 34536 bytes
//345989494 0 REQST build  GET http://hohum.com/foobar.html 
//345989494 0 INFO  build  http://hohum.com/foobar.html ip=4.5.6.7 : Success
//324234324 0 DEBUG build  Skipping xxx.com, would hammer IP.

#define MAX_LOG_MSGS  1024 // in memory

// this is for printing out how a page is parsed by PageParser.cpp
/* extern char *g_pbuf     ; */
/* extern char *g_pbufPtr  ; */
/* extern char *g_pterms   ; */
/* extern char *g_ptermPtr ; */
/* extern char *g_pend; */
extern char *g_dbuf;
extern int32_t  g_dbufSize;

#ifdef _CHECK_FORMAT_STRING_
bool log ( int32_t type , char *formatString , ... ) 
	__attribute__ ((format(printf, 2, 3)));
bool log ( char *formatString , ... )
	__attribute__ ((format(printf, 1, 2)));
bool logf ( int32_t type , char *formatString , ... )
	__attribute__ ((format(printf, 2, 3)));
#else
// may also syslog and fprintf the msg.
// ALWAYS returns FALSE (i.e. 0)!!!! so you can say return log.log(...)
bool log ( int32_t type , char *formatString , ... ) ;
// this defaults to type of LOG_WARN
bool log ( char *formatString , ... ) ;
// force it to be logged, even if off on log controls panel
bool logf ( int32_t type , char *formatString , ... ) ;
#endif

class Log { 

 public:

	// returns true if opened log file successfully, otherwise false
	bool init ( char *filename );

	// . log this msg
	// . "msg" must be NULL terminated
	// . now is the time of day in milliseconds since the epoch
	// . if "now" is 0 we insert the timestamp for you
	// . if "asterisk" is true we print an asterisk to indicate that
	//   the msg was actually logged earlier but only printed now because
	//   we were in a signal handler at the time
	bool logR ( int64_t now, int32_t type, char *msg, bool asterisk ,
		    bool forced = false );

	// returns false if msg should not be logged, true if it should
	bool shouldLog ( int32_t type , char *msg ) ;

	// just initialize with no file
	Log () ;
	~Log () ;

	void reset ( );

	void setPid();

	// save before exiting
	void close () { dumpLog();  };

	// do we need to print out the msgs we've saved while in sig handler?
	bool needsPrinting ( ) { return m_needsPrinting; };

	// print the stuff that needs printing
	void printBuf ( );

	// this is only called when in a signal handler
	bool logLater ( int64_t now , int32_t type , char *formatString , 
			va_list ap );

	bool          m_disabled;

	bool m_logTimestamps;

	char *getFilename() { return m_filename; };

 private:

	bool dumpLog ( ); // make room for the new ones

	char   *m_filename;
	int     m_fd;
	char   *m_hostname;
	int     m_port;

	int64_t m_logFileSize;
	bool makeNewLogFile ( );

	char         *m_errorMsg      [ MAX_LOG_MSGS ];
	int16_t     m_errorMsgLen   [ MAX_LOG_MSGS ];
	int64_t     m_errorTime     [ MAX_LOG_MSGS ];
	unsigned char m_errorType     [ MAX_LOG_MSGS ];
	int           m_numErrors;

	// m_erroMsg's point into this buf.
	char          m_buf           [ 1024 * 32];  
	int           m_bufPtr;

	// do we need to print out the msgs we've saved while in sig handler?
	bool          m_needsPrinting;
};

extern class Log g_log;

#endif
