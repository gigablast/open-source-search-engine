#include <errno.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>      // for opendir()
#include <dirent.h>         // for opendir()
#include <time.h>           // for time()
#include <ctype.h>
#include <sys/socket.h>  // inet_ntoa()
#include <netinet/in.h>  // inet_ntoa()
#include <arpa/inet.h>   // inet_ntoa()
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

// to prepare the log files for running this invoicer program:
// 1) cd to the proxylog directory
// 2) grep "INFO  http" proxylog-* | grep -v autoban > FINAL 
// 3) mkdir tmp ; mv proxylog-* tmp/
// 4) nohup split -b 2000000000 FINAL proxylog &

static char *iptoa ( long ip ) ;
char *strcasestr2 ( char *haystack , char *needle ) ;

#define TABLE_SIZE (100*1024)

class Client {
public:
	char   *m_company;    // name with spaces
	char   *m_company2;   // name for filename

	char   *m_name;       // dear john

	char   *m_startDate ;
	char   *m_active    ; // still active? yes or no?
	char   *m_sales     ;
	char   *m_code      ; // password for using the feed and for tagging
	char   *m_gigabits  ;

	char   *m_email     ;
	char   *m_ips       ; // space separated list of ips they feed from

	double m_prevBalance;  // in dollars
	double m_minCharge  ;  // in dollars

	double m_lastPayment;

	long   m_rpq        ;
	//double m_cpm1       ;  // in dollars (1st tier)
	//double m_cpm2       ;  // in dollars (2nd tier)
	//double m_cpm3       ;  // in dollars (3rd tier)
	long   m_tierSize1  ;  // search boundary for tier1
	long   m_tierSize2  ;  // search boundary for tier2

	double m_license    ;  // in dollars
	double m_hours      ;  // in hours, additional services in hours
	double m_hourlyRate ;  // in dollars, for m_hours
	long   m_percent    ;  // if started mid month, use 50%
	long   m_daysToPay  ;
	long   m_searches   ;

        double m_dumpFee ; // data dump

	// convenience var to point to up to 32 ips in m_ips
	char  *m_ipPtrs[50];

	unsigned long m_codeHash; // hash of m_code

	double m_total  ; // total owed

	double m_bill; // this month's bill

	double m_charge ; // ongoing count of how much they owe

	// temp variables
	char *m_cpmStr1;
	char *m_cpmStr2;
	char *m_cpmStr3;
	char *m_cpmGigabitsStr1;
	char *m_cpmGigabitsStr2;
	char *m_cpmGigabitsStr3;

	char *m_termNotice;
	char *m_termEffective;

	// cpm table
	// m_cpms[a][b][c] where
	// a = 1 if gigabits included, 0 if not included
	// b = tier #, can be 0, 1 or 2. tier # defined by "tier 1 max"
	//     and "tier 2 max"
	// c = 0 for n=10 
	//     1 for n=15
	//     2 for n=20
	//     3 for n=50
	//     4 for n=100
	//     5 for n=200
	//     6 for n=500
	//     7 for n=1000
	float m_cpms[2][3][8];

	// counts for each
	long m_counts[2][2][3][8];

	// counting cached page requests
	long m_cachedPageRequests;
};

// leech: 66.150.55.234
/*
static Client s_clients [ ] = {

	// signed Sep 6 2005
	{ "Mr. Reid"  ,  // Dear Sir line, john james
	  "K Position" ,  // Company Name
	  "KPosition" ,  // Company Name (no spaces)
	  "miles.reid@kposition.co.uk",
	  "9KP6PSN5" , // code password
	  "212.100.241.* 83.138.132.209 83.138.180.* 83.138.130.238", // ips
	  1200.00   , // previous balance in dollars (owes $390 deposit)
	  1500.00  , // monthly minimum charge

	  // 10 results per query, w/ gigabits
	  .98    , // CPM rate in dollars for 1st tier
	  500000  , // size of 1st tier
	  .77    , // CPM rate in dollars for 2nd tier
	  500000  , // size of 2nd tier
	  .64    , // CPM rate in dollars for 3rd tier

	  0.00  , // LICENSE charge fee for globalspec and newspaperarchive
	  0.00  , // hours of contracted work
	  0.00  , // hourly rate for services
	  100   , // percent monthly usage (for pro-rating min fee)
	  30    , // days to pay before late
	  0     , // searches --> we compute this
	  {NULL,NULL,NULL,NULL,NULL,NULL,NULL,
	   NULL,NULL,NULL,NULL,NULL,NULL,NULL},0,0.0 // internal use
	} ,
};
*/

typedef enum { 
	CLIENT_NAME = 0    ,
	CLIENT_SINCE       , 
	CLIENT_ACTIVE      ,
	CLIENT_SALESPERSON ,
	CLIENT_PASSCODE    ,
	CLIENT_IPS         ,
	CLIENT_RPQ         ,
	CLIENT_GIGABITS    ,
	CLIENT_TIER1_MAX   ,
	CLIENT_TIER2_MAX   ,
	CLIENT_TIER1_CPM   ,
	CLIENT_TIER2_CPM   ,
	CLIENT_TIER3_CPM   ,
	CLIENT_TIER1_GCPM  ,
	CLIENT_TIER2_GCPM  ,
	CLIENT_TIER3_GCPM  ,
	CLIENT_TERM_NOTICE ,
	CLIENT_TERM_EFF    ,
	CLIENT_PREV_BAL    ,
	CLIENT_MM          ,
	CLIENT_LIC_FEE     ,
	CLIENT_DUMP_FEE    ,
	CLIENT_HOURS       ,
	CLIENT_RATE        ,
	CLIENT_PERCENT     ,
	CLIENT_EMAIL       , 
	CLIENT_SALUT       };
	
typedef enum { 
	TYPE_STRING = 1 ,
	TYPE_LONG   = 2 ,
	TYPE_DOUBLE = 3 };


struct Field {
	char  *m_header;
	long   m_enum;
	long   m_type;
	void  *m_valPtr;
};

// the universal client template
class Client g_client;

#define MAX_CLIENTS 1000
Client s_clients[MAX_CLIENTS];
long   s_numClients = 0;

static Field s_fields[] = {
	{"Client Name" ,CLIENT_NAME, TYPE_STRING, &g_client.m_company },
	{"Start Date",CLIENT_SINCE, TYPE_STRING, &g_client.m_startDate },
	{"Active"      ,CLIENT_ACTIVE, TYPE_STRING , &g_client.m_active },
	{"Sales person",CLIENT_SALESPERSON,TYPE_STRING,&g_client.m_sales},
	{"Pass code"   ,CLIENT_PASSCODE ,TYPE_STRING , &g_client.m_code},
	{"IPs"         ,CLIENT_IPS ,TYPE_STRING , &g_client.m_ips},
	{"RPQ"         ,CLIENT_RPQ , TYPE_LONG ,&g_client.m_rpq}, //res per qry
	{"Giga Bits"   ,CLIENT_GIGABITS , TYPE_STRING ,&g_client.m_gigabits},
	{"Tier 1 Size"  ,CLIENT_TIER1_MAX , TYPE_LONG ,&g_client.m_tierSize1},
	{"Tier 2 Size"  ,CLIENT_TIER2_MAX , TYPE_LONG ,&g_client.m_tierSize2},
	//{"Tier 1 CPM"  ,CLIENT_TIER1_CPM , TYPE_DOUBLE ,&g_client.m_cpm1},
	//{"Tier 2 CPM"  ,CLIENT_TIER2_CPM , TYPE_DOUBLE ,&g_client.m_cpm2},
	//{"Tier 3 CPM"  ,CLIENT_TIER3_CPM , TYPE_DOUBLE ,&g_client.m_cpm3},

	{"Tier 1 CPM 10/15/20/50/100/200/500/1000"  ,
	 CLIENT_TIER1_CPM , TYPE_STRING , &g_client.m_cpmStr1},
	{"Tier 2 CPM 10/15/20/50/100/200/500/1000"  ,
	 CLIENT_TIER2_CPM , TYPE_STRING , &g_client.m_cpmStr2},
	{"Tier 3 CPM 10/15/20/50/100/200/500/1000"  ,
	 CLIENT_TIER3_CPM , TYPE_STRING , &g_client.m_cpmStr3},

	{"Gigabits Tier 1 CPM 10/15/20/50/100/200/500/1000"  ,
	 CLIENT_TIER1_GCPM , TYPE_STRING , &g_client.m_cpmGigabitsStr1},
	{"Gigabits Tier 2 CPM 10/15/20/50/100/200/500/1000"  ,
	 CLIENT_TIER2_GCPM , TYPE_STRING , &g_client.m_cpmGigabitsStr2},
	{"Gigabits Tier 3 CPM 10/15/20/50/100/200/500/1000"  ,
	 CLIENT_TIER3_GCPM , TYPE_STRING , &g_client.m_cpmGigabitsStr3},

	{"Termin Notice Date",CLIENT_TERM_NOTICE, TYPE_STRING ,
	 &g_client.m_termNotice},
	{"Termin Date"   ,CLIENT_TERM_EFF , TYPE_STRING ,
	 &g_client.m_termEffective},

	{"Prev Bal"   ,CLIENT_PREV_BAL , TYPE_DOUBLE ,&g_client.m_prevBalance},
	{"Min Monthly Fee"   ,CLIENT_MM , TYPE_DOUBLE ,&g_client.m_minCharge},
	{"Monthly SW Lic Fee",CLIENT_LIC_FEE ,TYPE_DOUBLE,&g_client.m_license},
	{"Data Dump Fee",CLIENT_DUMP_FEE ,TYPE_DOUBLE,&g_client.m_dumpFee},
	{"Hours of Labor"    ,CLIENT_HOURS ,TYPE_DOUBLE ,&g_client.m_hours},
	{"Hourly Rate"      ,CLIENT_RATE ,TYPE_DOUBLE ,&g_client.m_hourlyRate},
	{"Percent Monthly Usage",CLIENT_PERCENT,TYPE_LONG,&g_client.m_percent},
	{"Email"             ,CLIENT_EMAIL , TYPE_STRING ,&g_client.m_email},
	{"Salutation"        ,CLIENT_SALUT , TYPE_STRING ,&g_client.m_name}
};

char g_buf[200000];

// returns 0 on success
long loadClients() {
	// open the file master.txt
	char *ff = "./master.txt";
	// read in all of file right away
	int fd = open ( ff , O_RDONLY );
	if ( fd < 0 ) {
		fprintf(stderr,"open %s : %s\n",ff,strerror(errno));
		return -1;
	}
	// . first line is the tab delimited headers
	// . find the column number of each of our headers
	static char s[100024];
	if ( read ( fd , s , 100000 ) < 0 ) {
		fprintf(stderr,"read %s : %s\n",ff,strerror(errno));
		return -1;
	}
	// map the column to a field descriptor, s_fields
	long fieldDesc[100];
	// init to all -1
	for ( long i = 0 ; i < 100 ; i++ ) fieldDesc[i] = -1;
	// the column number 
	long col = 0;
	// point to start of file
	char *p = s;

	char matched[200];
	memset ( matched , 0 , 200 );
	long maxCol = -1;
	
	// loop over all field headers
	while ( *p != '\n' && *p != '\r' ) {
		// skip spaces
		while ( *p == ' ' ) p++;
		// skip a quote
		if ( *p == '\"' ) p++;
		// find the tab or end
		char *pend = p;
		while ( *pend && *pend != '\t' && *pend != '\r' ) pend++;
		// get lenth
		long plen = pend - p;
		// if ended in quote, discount
		if ( *(pend-1) == '\"' ) plen--;
		// watch out
		if ( col >= 100 ) {
			// if that was it, break out
			break;
			fprintf(stderr,"Too many columns in spreadsheet.\n");
			return -1;
		}
		// assume column does not match a header in our list
		fieldDesc[col] = -1;
		// find this field name in our list
		for ( long j = 0 ; j < sizeof(s_fields)/sizeof(Field) ; j++ ) {
			// skip empties
			if ( pend - p == 0 ) continue;
			if ( strncasecmp(p,s_fields[j].m_header,plen) ) 
				continue;
			// . we got a match
			// . map column number to a header number
			fieldDesc[col] = j;
			// log it
			//fprintf(stderr,"found \"%s\" at column #%li "
			//	"in master.txt.\n",s_fields[j],col);
			// get max col
			maxCol = col;
			// make sure we matched all
			matched[j] = 1;
			break;
		}
		// inc column count
		col++;
		// update the pointer
		p = pend;
		if ( *p == '\t' ) p++;
	}

	// next line
	while ( *p == '\n' || *p == '\r' ) p++;

	// clear all clients
	for ( long i = 0 ; i < (long)MAX_CLIENTS ; i++ ) 
		memset(&s_clients[i],0,sizeof(Client));

	// ensure all fields had a match in spreadsheet
	for ( long j = 0 ; j < sizeof(s_fields)/sizeof(Field) ; j++ ) {
		if ( matched[j] ) continue;
		fprintf(stderr,"Did not match field \"%s\" in spreadsheet.\n",
			s_fields[j].m_header);
		exit(0);
	}

	long nc = 0;
	// store company names without spaces in here
	char *dst = g_buf;
	// the column number
	col = 0;
	// now do each client
	while ( *p ) {
		// skip spaces
		while ( *p == ' ' ) p++;
		// is there a quote?
		char inquote = 0;
		if ( *p == '\"' ) { inquote = 1; p++; }
		// find the tab or end
		char *pend = p;
		while ( *pend && *pend != '\"' && *pend != '\t' &&
			*pend != '\r' ) pend++;

		// get current client
		Client *c = &s_clients[nc];

		//c->m_percent   = 100;
		c->m_daysToPay = 20;

		// is it a recognized field? this is -1 if not
		long fn = fieldDesc[col];
		if ( col > maxCol ) 
			fn = -1;
		
		if ( fn >= 0 ) {
			// get the field descriptor
			Field *f = &s_fields[fn];
			// get offset of thing
			long off = (char *)f->m_valPtr - (char *)&g_client;
			if ( off > sizeof(Client)) {char *xx = NULL; *xx = 0;}
			// what to set
			char *val = ((char *)c + off);
			// assign to client class
			if ( f->m_type == TYPE_STRING )	{
				*(char  **)val=p;
			}
			else {
				// bitch and exit if not a number and not N/A
				/*
				if ( (c->m_active == 'Y' ||
				     c->m_active == 'y' ) &&
				     !isdigit(*p) && *p!='.' && *p!='N' &&
				     *p !='n' ) {
					fprintf(stderr,"Bad numeric field for "
						"%s in master.txt.\n",
						c->m_company);
					return -1;
				}
				*/
				char  tmp[10000];
				char *src = p;
				char *dst = tmp;
				for ( ; src < pend ; src++ ) {
					if ( *src == ',' ) continue;
					if ( dst-tmp > 9000 ) {
						char *xx = NULL; *xx = 0; }
					*dst++ = *src;
				}
				*dst = 0;
				if ( f->m_type == TYPE_LONG  )
					*(long   *)val=atol(tmp);
				if ( f->m_type == TYPE_DOUBLE)
					*(double *)val=atof(tmp);
			}
		}

		// save
		char *save = pend;
		// update p
		p = pend;
		if ( *p == '\"' ) p++;
		if ( *p == '\t' ) p++;
		// and column number
		col++;
		// next client? (skip over \n, too)
		if ( *p == '\r' ) { 
			p++; p++; 
			// only if this client was valid
			if ( s_clients[nc].m_company &&
			     s_clients[nc].m_company[0] &&
			     s_clients[nc].m_company != save ) {
				// first set company2, like company1 but no
				// spaces
				char *src = s_clients[nc].m_company;
				s_clients[nc].m_company2 = dst;
				for ( ; src < pend ; src++ ) {
					if ( *src == ',' ) continue;
					if ( *src == ' ' ) continue;
					if ( ispunct(*src) ) continue;
					if ( dst-g_buf > 190000 ) {
						char *xx = NULL; *xx = 0; }
					*dst++ = *src;
				}
				*dst++ = 0;
				// hack fix, cinco puts cpms in cents, that
				//c->m_cpm1 /= 100;
				//c->m_cpm2 /= 100;
				//c->m_cpm3 /= 100;
				nc++; 
				if ( nc == 31 )
					nc = 31;
			}
			col=0; 
		}
		// NULL terminate it
		*save = '\0';
	}

	// set all the tier cpm levels
	for ( long i = 0 ; i < nc ; i++ ) {

		char *p;
		// . get prices for tier1 based on # results per query
		// . tier1 size is s_clients[i].m_tierSize1
		p = s_clients[i].m_cpmStr1;
		sscanf ( p , "%f/%f/%f/%f/%f/%f/%f/%f",
			 &s_clients[i].m_cpms[0][0][0],
			 &s_clients[i].m_cpms[0][0][1],
			 &s_clients[i].m_cpms[0][0][2],
			 &s_clients[i].m_cpms[0][0][3],
			 &s_clients[i].m_cpms[0][0][4],
			 &s_clients[i].m_cpms[0][0][5],
			 &s_clients[i].m_cpms[0][0][6],
			 &s_clients[i].m_cpms[0][0][7]);
		p = s_clients[i].m_cpmStr2;
		sscanf ( p , "%f/%f/%f/%f/%f/%f/%f/%f",
			 &s_clients[i].m_cpms[0][1][0],
			 &s_clients[i].m_cpms[0][1][1],
			 &s_clients[i].m_cpms[0][1][2],
			 &s_clients[i].m_cpms[0][1][3],
			 &s_clients[i].m_cpms[0][1][4],
			 &s_clients[i].m_cpms[0][1][5],
			 &s_clients[i].m_cpms[0][1][6],
			 &s_clients[i].m_cpms[0][1][7]);
		p = s_clients[i].m_cpmStr3;
		sscanf ( p , "%f/%f/%f/%f/%f/%f/%f/%f",
			 &s_clients[i].m_cpms[0][2][0],
			 &s_clients[i].m_cpms[0][2][1],
			 &s_clients[i].m_cpms[0][2][2],
			 &s_clients[i].m_cpms[0][2][3],
			 &s_clients[i].m_cpms[0][2][4],
			 &s_clients[i].m_cpms[0][2][5],
			 &s_clients[i].m_cpms[0][2][6],
			 &s_clients[i].m_cpms[0][2][7]);


		p = s_clients[i].m_cpmGigabitsStr1;
		sscanf ( p , "%f/%f/%f/%f/%f/%f/%f/%f",
			 &s_clients[i].m_cpms[1][0][0],
			 &s_clients[i].m_cpms[1][0][1],
			 &s_clients[i].m_cpms[1][0][2],
			 &s_clients[i].m_cpms[1][0][3],
			 &s_clients[i].m_cpms[1][0][4],
			 &s_clients[i].m_cpms[1][0][5],
			 &s_clients[i].m_cpms[1][0][6],
			 &s_clients[i].m_cpms[1][0][7]);
		p = s_clients[i].m_cpmGigabitsStr2;
		sscanf ( p , "%f/%f/%f/%f/%f/%f/%f/%f",
			 &s_clients[i].m_cpms[1][1][0],
			 &s_clients[i].m_cpms[1][1][1],
			 &s_clients[i].m_cpms[1][1][2],
			 &s_clients[i].m_cpms[1][1][3],
			 &s_clients[i].m_cpms[1][1][4],
			 &s_clients[i].m_cpms[1][1][5],
			 &s_clients[i].m_cpms[1][1][6],
			 &s_clients[i].m_cpms[1][1][7]);
		p = s_clients[i].m_cpmGigabitsStr3;
		sscanf ( p , "%f/%f/%f/%f/%f/%f/%f/%f",
			 &s_clients[i].m_cpms[1][2][0],
			 &s_clients[i].m_cpms[1][2][1],
			 &s_clients[i].m_cpms[1][2][2],
			 &s_clients[i].m_cpms[1][2][3],
			 &s_clients[i].m_cpms[1][2][4],
			 &s_clients[i].m_cpms[1][2][5],
			 &s_clients[i].m_cpms[1][2][6],
			 &s_clients[i].m_cpms[1][2][7]);
	}

	// save # of clients
	s_numClients = nc;
	// be nice
	close(fd);
	// success, return 0
	return 0;
}




// returns 0 on success
long loadOldSummary() {

	// get old month name and year
	char prevMonthName[32];
	char prevYearName [32];
	time_t t = time(NULL);
	t -= 50*24*3600 ;
	struct tm *timeStruct = localtime ( &t );
	strftime ( prevMonthName , 32 , "%b" , timeStruct );
	strftime ( prevYearName  , 32 , "%Y" , timeStruct );

	// make filename
	char ff[60];
	sprintf(ff,"../%s%s/SUMMARY",prevMonthName,prevYearName);

	// note it
	fprintf(stderr,"Loading %s\n",ff);
	
	// read in all of file right away
	int fd = open ( ff , O_RDONLY );
	if ( fd < 0 ) {
		fprintf(stderr,"open %s : %s\n",ff,strerror(errno));
		return -1;
	}
	// . first line is the tab delimited headers
	// . find the column number of each of our headers
	char s[100024];
	if ( read ( fd , s , 100000 ) < 0 ) {
		fprintf(stderr,"read %s : %s\n",ff,strerror(errno));
		return -1;
	}

	// scan each line
	for ( char *p = s; *p ; p++ ) {
		if ( *p != '$' ) continue;
		if ( p!=s && p[-1]!='\n' ) continue;
		// we got a line
		// skip $
		p++;
		// skip spaces
		while ( isspace(*p) ) p++;
		// skip numbers
		while ( isdigit(*p) ) p++;
		// must be a '.'
		if ( *p != '.' ) { 
			fprintf(stderr,"invoicer: no $xx.yy on line in "
				"%s\n",ff);
			return -1;
		}
		// skip .
		p++;
		// skip two digits
		p += 2;
		// skip single space
		if ( *p !=' ' ) {
			fprintf(stderr,"invoicer: no space after or improper "
				"dollar amount in %s\n",ff);
			return -1;
		}
		p++;
		// this is the name
		char *name = p;
		// get "total owed"
		char *to = strstr ( name , "total owed=$" );
		// crazy!
		if ( ! to ) { 
			fprintf(stderr,"invoicer: no total owed on line in "
				"%s\n",ff);
			return -1;
		}
		// temp null the name
		char *tmp = name;
		while ( *tmp!='\n' && *tmp && *tmp != '(' ) tmp++;
		// need it
		if ( *tmp != '(' ) {
			fprintf(stderr,"invoicer: no \"(searches=\" after "
				"company name in %s\n",ff);
			return -1;
		}
		// back up as long as a space or tab
		while ( tmp[-1]==' ' || tmp[-1] == '\t' ) tmp--;
		// term it for print out "ambiguous company name" msg below
		*tmp = '\0';
		// point to dollar amount
		to += 12;
		// skip spaces
		while ( isspace(*to) ) to++;
		// must be digit or a negative amount sign
		if ( ! isdigit(*to) && *to !='-' ) { 
			fprintf(stderr,"invoicer: no digit in total owed in "
				"%s\n",ff);
			return -1;
		}
		// skip it
		p = to;
		// convert it
		float owed = atof(to);
		// count matches
		long matches = 0;
		// find in the client list
		for ( long i = 0 ; i < s_numClients ; i++ ) {
			// get client name
			char *cn = s_clients[i].m_company;
			// length
			long clen = strlen(cn);
			// min
			if ( clen <= 1 ) continue;
			// truncate
			if ( clen > 12 ) clen = 12;
			//if ( i == 20 )
			//fprintf(stderr,"invoicer: checking %s with #%li "
			//"%s\n",name,i,cn);
			// compare to "name"
			if ( strncmp ( name , cn , strlen(name) ) ) continue;
			// got a match
			matches++;
			// ignore if we already matched
			if ( matches > 1 ) continue;
			//fprintf(stderr,"invoicer: matched %s to %li\n",
			//	name,i);
			// set the prev balance
			s_clients[i].m_prevBalance = owed;
		}
		// sanity check
		if ( matches > 1 ) {
			fprintf(stderr,"invoicer: ambiguous company name "
				"\"%s\"\n",name);
			//return -1;
		}
		// MUST MATCH
		if ( matches == 0 ) {
			fprintf(stderr,"invoicer: did not match client "
				"\"%s\" in "
				"%s file to a client in master.txt\n "
				"",
				name,ff);
			return -1;
		}
	}
	// be nice
	close(fd);


// have a file called "payments" like this:
// mwells@gk37:/mnt/raid1/logs/May2009$ cat PAYMENTS
// $  600.00 23half Inc.  05/09/09
// $ 3022.50 Lumifi       05/09/09
// $ 1796.50 ISEEK        05/14/09
// $ 3562.70 TurnCommerce 05/14/09
// $ 6498.17 Ixquick      05/15/09 (includes correction)
// $ 5262.37 Vivisimo1    05/20/09
// $  500.00 Morrison Med 05/29/09mwells@gk37:/mnt/raid1/logs/May2009$

	// read in all of file right away
	char *pf = "./PAYMENTS";

	// note it
	fprintf(stderr,"Loading %s\n",pf);


	fd = open ( pf , O_RDONLY );
	if ( fd < 0 ) {
		fprintf(stderr,"open %s : %s\n",pf,strerror(errno));
		return -1;
	}
	// . first line is the tab delimited headers
	// . find the column number of each of our headers
	long nb = read ( fd , s , 100000 ) ;
	if ( nb < 0 ) {
		fprintf(stderr,"read %s : %s\n",pf,strerror(errno));
		return -1;
	}
	// null term it
	s[nb] = '\0';
	// shave off trailing \n's
	while ( nb>0 && s[nb-1]=='\n' ) 
		s[--nb]='\0';

	//long cnt = 0;
	// scan each line
	for ( char *p = s; *p ; p++ ) {
		// skip if not beggining of a line
		if ( p!=s && p[-1]!='\n' ) continue;
		// skip if empty line
		if ( *p == '\n' ) continue;
		//fprintf(stderr,"invoicer: line #%li",cnt++);
		// otherwise, must start with $
		if ( *p != '$' ) { 
			fprintf(stderr,"invoicer: line in %s does not "
				"start with $\n",pf); 
			return -1; 
		}
		// skip $
		p++;
		// skip spaces
		while ( isspace(*p) ) p++;
		// mark it
		char *ps = p;
		// skip numbers
		while ( isdigit(*p) ) p++;
		// must be a '.'
		if ( *p != '.' ) { char *xx=NULL;*xx=0; }
		// skip .
		p++;
		// skip two digits
		p += 2;
		// skip single space
		if ( *p !=' ' ) { 
			fprintf(stderr,"invoicer: no space after or improper "
				"dollar amount in %s\n",pf);
			return -1;
		}
		p++;
		// convert it
		float payment = atof(ps);
		// this is the name
		char *name = p;
		// count matches
		long matches = 0;
		// find in the client list
		for ( long i = 0 ; i < s_numClients ; i++ ) {
			// get client name
			char *cn = s_clients[i].m_company;
			// length
			long clen = strlen(cn);
			// truncate
			if ( clen > 12 ) clen = 12;
			// compare to "name"
			if ( strncmp ( name , cn , clen ) ) continue;
			// got a match
			matches++;
			// do not double count
			if ( matches > 1 ) continue;
			// set the prev balance
			s_clients[i].m_lastPayment = payment;
		}
		// sanity check
		//if ( matches > 1 ) {
		//	fprintf(stderr,"invoicer: ambiguous company name\n");
		//	return -1;
		//}
		// MUST MATCH
		if ( matches == 0 ) {
			fprintf(stderr,"invoicer: did not match payment in "
				"%s file to a client in master.txt\n",
				pf);
			return -1;
		}
	}
	// be nice
	close(fd);


	// success, return 0
	return 0;
}


unsigned long long g_hashtab[256][256] ;

bool hashinit () {
	static bool s_initialized = false;
	// bail if we already called this
	if ( s_initialized ) return true;
	// show RAND_MAX
	//printf("RAND_MAX = %lu\n", RAND_MAX ); it's 0x7fffffff
	// seed with same value so we get same rand sequence for all
	srand ( 1945687 );
	for ( long i = 0 ; i < 256 ; i++ )
		for ( long j = 0 ; j < 256 ; j++ ) {
			g_hashtab [i][j]  = (unsigned long long)rand();
			// the top bit never gets set, so fix
			if ( rand() > (0x7fffffff / 2) ) 
				g_hashtab[i][j] |= 0x80000000;
			g_hashtab [i][j] <<= 32;
			g_hashtab [i][j] |= (unsigned long long)rand();
			// the top bit never gets set, so fix
			if ( rand() > (0x7fffffff / 2) ) 
				g_hashtab[i][j] |= 0x80000000;
		}
	//if ( g_hashtab[0][0] != 6720717044602784129LL ) return false;
	s_initialized = true;
	return true;
}

unsigned long hash32 ( const char *s, long len ) {
	unsigned long h = 0;
	long i = 0;
	while ( i < len ) {
		h ^= (unsigned long) g_hashtab [(unsigned char)i]
			[(unsigned char)s[i]];
		i++;
	}
	return h;
}

int main ( int argc , char *argv[] ) {

	long buckets[TABLE_SIZE];
	long counts [TABLE_SIZE];
	memset ( counts  , 0 , TABLE_SIZE * 4 );
	memset ( buckets , 0 , TABLE_SIZE * 4 );

	// first arg is the directory of log* files
	if ( argc < 2 || argc > 3 ) {
		fprintf(stderr,"Usage:\tinvoicer <dir of log files> [-ef]\n");
		fprintf(stderr,"\tthe -e flag will email the emails\n");
		fprintf(stderr,"\tthe -f flag will print raw leechers\n");
		return -1;
	}

	if ( ! hashinit () ) return 0;

	// read in the microsoft excel text file of clients
	if ( loadClients() ) return 0;

	// now read in the last SUMMARY file to get their prev balances
	// without any payments recorded since last invoice session
	if ( loadOldSummary() ) return 0;

	// print the clients out
	for ( long i = 0 ; i < s_numClients ; i++ ) {
		char *name = s_clients[i].m_company;
		char t = name[12];
		name[12]='\0';
		fprintf(stdout,"loaded client=%012s active=%s "
			"prevBal=%10.02f lastPayment=%9.02f\n"
			//"passcode=%s "
			//"email=%s salut=\"%s\"\n",
			,
			//s_clients[i].m_company,
			name,//s_clients[i].m_company2,
			s_clients[i].m_active,
			s_clients[i].m_prevBalance,
			s_clients[i].m_lastPayment);
			//s_clients[i].m_code,
			//s_clients[i].m_email,
			//s_clients[i].m_name ); // salutation = dear john, ...
		name[12]=t;
	}

	// make sure we don't have any dups because filenames have to be uniq
	for ( long i = 0 ; i < s_numClients ; i++ ) {
		long counter = '2';
	again:
		for ( long j = 0 ; j < i ; j++ ) {
			if ( strcmp ( s_clients[i].m_company2 ,
				      s_clients[j].m_company2 ) != 0) continue;
			// we got a dup, change the name
			long len = strlen(s_clients[i].m_company2);
			// if not the first time, adjust
			if ( counter != '2' ) len--;
			s_clients[i].m_company2[len+0] = counter++;
			s_clients[i].m_company2[len+1] = '\0';
			// log it
			fprintf(stdout,"company %s is a dup, "
				"pls change to %s ?\n",
				s_clients[j].m_company2,
				s_clients[i].m_company2);
			return -1;
			//goto again;
		}
	}

	// open the dir and scan for log files
	char *dirname = argv[1];
	DIR *edir = opendir ( dirname );
	if ( ! edir ) {
		fprintf ( stderr, "invoicer::opendir (%s):%s\n",
				   dirname,strerror( errno ) );
		return -1;
	}

	// set Client::m_ipPtrs[] member for each client
	//long nc = sizeof(s_clients)/sizeof(Client);
	long nc = s_numClients;
	for ( long i = 0 ; i < nc ; i++ ) {
		char *s = s_clients[i].m_ips;
		long  n = 0;
		while ( *s ) {
			s_clients[i].m_ipPtrs[n++] = s;
			// skip to next
			while ( *s && *s != ' ' ) s++;
			// skip over spaces
			while ( *s && *s == ' ' ) s++;
			// break if done, otherwise set more ptrs
			if ( ! *s ) break;
		}
		// NULL terminate list of ptrs
		s_clients[i].m_ipPtrs[n] = NULL;
		// now set their code hash
		s = s_clients[i].m_code;
		unsigned long h = hash32 ( s , strlen(s) );
		if ( h == 0 ) h = 1;
		s_clients[i].m_codeHash = h;
	}
	// should we just filter out the raw leechers?
	char doFilter = 0;
	if ( argc == 3 && argv[2][0]=='-' && argv[2][1]=='f' ) doFilter = 1;
	/////////////////////////////
	//
	// EMAIL
	//
	/////////////////////////////
	// optional -e flag after dir to email the emails saved to files in dir
	if ( argc == 3 && argv[2][0]=='-' && argv[2][1]=='e' ) {
		// loop over all the log files in this directory
		struct dirent *ent;
		while ( (ent = readdir ( edir ))  ) {
			char *filename = ent->d_name;
			if ( strncasecmp ( filename , "email." , 6 ) != 0 ) 
				continue;
			// skip if ends in a ~, it is an emacs backup file
			if ( filename[strlen(filename)-1] == '~' ) continue;
			// make a full filename
			char full[1024];
			sprintf ( full , "%s/%s", dirname,filename);
			// . we got one, process it
			// . returns -1 on failure
			//long i = O_LARGEFILE;
			FILE *fd = fopen ( full , "r" );
			if ( ! fd ) {
				fprintf(stderr,"open %s : %s\n",
					full,strerror(errno));
				return -1;
			}
			// first line is email addresses
			char email[1024];
			fgets ( email , 1023 , fd );
			email[strlen(email)-1]='\0';
			// then subject
			char subj[1024];
			fgets ( subj , 1023 , fd );
			subj[strlen(subj)-1]='\0';
			// debug 
			//sprintf(email,"mattdwells@hotmail.com");
			// send it out
			char cmd[1024];
			sprintf ( cmd , 
				  "cat %s | "
				  "/usr/bin/mail "
				  //"-b accounting@gigablast.com "
				  //"-s \"%s\" %s -- -f gbacc@mail.com\n", 
				  //"-s \"%s\" %s -- -f gigablast@mail.com\n", 
				  "-s \"%s\" %s -- -f mwells2@gigablast.com\n", 
				  filename,
				  subj , 
				  email );

			printf ( "%s\n", cmd);
			long st = 0;
			st = system ( cmd );	
			if ( st == 127 ) 
			       fprintf(stderr,"system called had error 127\n");
			if ( st == -1  ) 
			        fprintf(stderr,"system called had error -1\n");
			fclose(fd);
			//return 0;
		}
		return 0;
	}
	/////////////////////////////////
	//
	// END EMAIL
	//
	/////////////////////////////////

	long totalSearches = 0;
	long count  = 0;
	long count2 = 0;
	long leeches = 0;

	// hash table for partner names
	char *pslots[1024];
	// and corresponding query counts
	long   pcounts[1024];
	// and where we store the partner ids
	char pnames[3000];
	// clear both
	memset ( pslots , 0 , 1024 * 4 );
	memset ( pcounts , 0 , 1024 * 4 );
	char *pbufPtr = pnames;

	//stop after 100000 line
	//long lineCount = 1000000;

	// loop over all the log files in this directory
	struct dirent *ent;
	while ( (ent = readdir ( edir ))  ) {
	char *filename = ent->d_name;
	if ( strncasecmp ( filename , "log"      , 3 ) != 0 &&
	     strncasecmp ( filename , "proxylog" , 8 ) != 0    )
		continue;
	// make a full filename
	char full[1024];
	sprintf ( full , "%s/%s", dirname,filename);

	// . we got one, process it
	// . this will set the m_searches field of each customer
	// . returns -1 on failure
	// open the log file
	FILE *fd = fopen ( full , "r" );
	if ( ! fd ) {
		fprintf(stderr,"open %s : %s\n",full,strerror(errno));
		return -1;
	}
	// skip it
	long nn = totalSearches;
	printf("processing %s\n",full);


	long newIxquickCode = hash32("ixqy8ej29",9);
	long oldIxquickCode = hash32("ixquickRhB",10);

	// go through each line in this log file
	char buf [ 3000 ];
	while ( fgets ( buf , 3000 , fd ) ) {
		// debug hack
		//if ( --lineCount <= 0 ) break;
		// sleep for 200 ms every 100000 lines we process so we do not
		// affect performance of gigablast
		//if ( ++count  == 150000 ) { usleep(200000); count = 0; };
		// debug msg
		if ( ! doFilter && (count2 % 30000) == 0 ) 
			printf ("line #%li\n",count2);
		count2++;
		// it must have a GET / in it
		//if ( ! strstr ( buf , "GET /" ) &&
		//     ! strstr ( buf , "HEAD /" ) ) continue;
		// ip should be 6th field (space separated)
		// BUT 8th for the new format of logs
		bool ipoff = 4;
		char *s = buf;
		long  n = 0;
		long  cachedPageRequest = 0;
		// debug point
		//if ( strstr(s,"207.97.211.") )
		//	fprintf(stderr,"hey\n");
		// skip till we hit the first .
		while ( *s && *s != '.' ) s++;
		// backup to start
		while ( s > buf && (*(s-1)>='0' && *(s-1)<='9')) s--;
		//while ( *s ) if ( *s++ == ' ' && n++ == 4 ) break;
		// skip line if no ip
		if ( ! *s ) continue;
		if ( *s < '0' || *s > '9' ) continue;
		// if it was from a local ip, skip it, probably from blaster
		if ( ! strncmp ( s , "127.0.0.1 ", 10 ) ) continue;
		// count "GET /get" requests times 8
		if ( strstr (s," /get?"         ) ) cachedPageRequest = 1;
		if ( strstr (s," /scroll.html?" ) ) cachedPageRequest = 1;
		// it must have a "q=" or "plus=" in it to be a query
		// now it can have a "d=" to get a docid
		char *t;
		for ( t = s ; *t ; t++ ) {
			if ( *(t+0)=='q' && *(t+1)=='=' ) break;
			if ( *(t+0)=='p' && *(t+1)=='l' && *(t+2)=='u' &&
			     *(t+3)=='s' && *(t+4)=='=' ) break;
			if ( *(t+0)=='d' && *(t+1)=='=' ) break; // cached page
		}
		// continue if no "q=" or "plus=" was present in the url
		// unless it was a POST /search
		if ( ! *t && ! cachedPageRequest &&
		     ! strcasestr2 ( s , "POST /search") ) continue;
		// ensure no " denied" ("robot denied","permission denied")
		if ( strstr ( s , " denied" ) ) continue;
		// autoban rejection
		// count these for now because the proxy had a bug in it
		if ( strstr ( s , "autoban " ) ) continue;
		// discount these Took 2 ms for request...
		if ( strstr ( s , " Took " ) ) continue;
		if ( strstr ( s , "ms for request" ) ) continue;
		// count total searches for shits and giggles
		totalSearches++;
		// debug point
		//if ( strncasecmp ( s , "63.245.51.", 10 ) == 0 )
		//	fprintf(stderr,"hey\n");
		// first look for the code
		char found = 0;
		Client *mc = NULL;
		long h;
		long  tlen;
		for ( t = s ; *t ; t++ ) 
			if ( *t=='c' && *(t+1)=='o' &&
			     strncasecmp(t+2,"de=",3)==0) break;
		if ( ! *t ) goto nocode;
		// hash the code
		t += 5;
		tlen = 0;
		while ( t[tlen] && t[tlen] != '&' && isalnum(t[tlen]) ) tlen++;
		if ( tlen == 0 ) goto nocode; // empty?
		// hash it!
		h = hash32 ( t , tlen );
		// map ixquicks new code to old code
		if ( h == newIxquickCode ) h = oldIxquickCode;
		// does it match somewhere?
		for ( long j = 0 ; j < nc ; j++ ) {
			if ( s_clients[j].m_codeHash == h ) {
				s_clients[j].m_searches++;
				found = 1;
				mc = &s_clients[j];
				// debug
				//if ( strstr ( s , "mamma" ) )
				//    fprintf(stderr,"code for %s",t);
				goto found;
			}
		}

	nocode:
		// compare the ip to all we have
		found = 0;
		mc = NULL;
		for ( long j = 0 ; j < nc ; j++ ) {
			// compare to the ip to each ip of the jth client
			for ( long k = 0 ; s_clients[j].m_ipPtrs[k] ; k++ ) {
				char *ipp = s_clients[j].m_ipPtrs[k];
				// first 4 chars must always match, do quickly
				if ( ipp[0] != s[0] ) continue;
				if ( ipp[1] != s[1] ) continue;
				if ( ipp[2] != s[2] ) continue;
				if ( ipp[3] != s[3] ) continue;
				// now ipp can end in a *, so match up to that
				long nm = 0;
				while ( ipp[nm] == s[nm] &&
					s[nm] != ' ' && ipp[nm]!=' ' ) nm++;
				// if match stopped because ipp hit a * OR
				// or because s hit a space it was good!
				if ( ipp[nm] != '*' && s[nm]!=' ') continue;
				s_clients[j].m_searches++;
				found = 1;
				mc = &s_clients[j];
			}
		}
	found:

		// keep stats on partners, just query counts
		char *partner = strstr(buf,"partner=");
		if ( partner ) {
			char *pc = partner + 8;
			char *pcend = pc ;
			for ( ; *pcend && isalnum(*pcend) ; pcend++ );
			// tmp null term for strcasecmp
			char saved = *pcend;
			*pcend = '\0';
			// hash it
			unsigned long ph = hash32(pc,pcend-pc);
			// first time we've seen it?
			// assume 1024 slots in table
			long n = (ph % 1024);
			while ( pslots[n] && strcasecmp(pslots[n],pc) ) 
				if ( ++n >= 1024 ) n = 0;
			// if NOT already added, then add it
			if ( ! pslots[n] ) {
				// include \0
				long size = pcend - pc + 1;
				memcpy(pbufPtr,pc,size);
				pslots[n] = pbufPtr;
				pbufPtr += size;
				
			}
			// and inc the count for stats
			pcounts[n]++;
			// restore
			*pcend = saved;
		}



		char *gigptr = strstr(buf,"nrt=");
		long gigabits = 0;
		if ( gigptr ) {
			// make sure a digit follows, some clients just
			// have &nrt=0
			char *p = gigptr + 4;
			while ( *p && isdigit(*p) ) {
				if ( *p != '0' ) { gigabits = 1; break; }
				p++;
			}
		}

		// . did they do a CTS query? 4 times more expensive.
		// . i.e. did they have sites= or UOR in the query?
		char  cts    = 0;
		char *uorptr = strstr(buf,"UOR+");
		char *sites  = strstr(buf,"sites=");
		// sites must start with a valid site
		if ( sites ) {
			sites += 6;
			while ( *sites && *sites!='&' && !isalnum(*sites) ) 
				sites++;
			if      ( *sites == '\0' ) sites = NULL;
			else if ( *sites == '&'  ) sites = NULL;
			if ( sites ) cts = 1;
		}
		// uor query?
		if ( uorptr ) cts = 1;

		// how many results did they get?
		char *pp = strstr ( buf , "n=" );
		long  an = 0; //10;
		long  on = 10;
		if ( ! pp            ) goto nonum;
		if ( pp <= buf       ) goto nonum;
		if ( isalnum(pp[-1]) ) goto nonum;
		on = atoi(pp+2);
		// round up
		if      ( on <= 10  ) an = 0;//10;
		else if ( on <= 15  ) an = 1;//15;
		else if ( on <= 20  ) an = 2;//20;
		else if ( on <= 50  ) an = 3;//50;
		else if ( on <= 100 ) an = 4;//100;
		else if ( on <= 200 ) an = 5;//200;
		else if ( on <= 500 ) an = 6;//500;
		//else if ( on <= 1000) an = 7;//1000;
		else                  an = 7;//1000;

	nonum:

		if ( found ) {
			long tier = 0;
			// these are ranges, not actual tier sizes
			if ( mc->m_searches > 
			     mc->m_tierSize1                   ) tier = 1;
			if ( mc->m_searches > 
			     mc->m_tierSize1 + mc->m_tierSize2 ) tier = 2;
			// look up the price
			double cost = mc->m_cpms[gigabits][tier][an];
			// multiply by 4 if doing a UOR/sites= query
			// eurekster's code = E5KST25R5
			if ( cts &&
			     mc->m_code[0]=='E' &&
			     mc->m_code[1]=='5' &&
			     mc->m_code[2]=='K' &&
			     mc->m_code[3]=='S' &&
			     mc->m_code[4]=='T' )
				// eurekster gets cts at normal cost for now
				// until 1 year from their contract signing
				cts = 0;
			if ( cts ) cost *= 4;
			// bill page requests at 8x, n=10 and no gigabits rate
			if ( cachedPageRequest ) 
				cost = 8 * mc->m_cpms[0][tier][0];
			// add it up
			mc->m_charge += cost ;
			// count each tier
			if ( cachedPageRequest ) {
				//mc->m_counts[0][0][tier][0]++;
				mc->m_cachedPageRequests++;
			}
			else
				mc->m_counts[cts][gigabits][tier][an]++;
		}

		// is it different than what's in master.txt?
		//double boost = 1.0;
		//if ( mc && mc->m_rpq != an ) {
		//	fprintf(stdout,"client %s rpq=%li on=%li "
		//		"an=%li buf=%s\n",
		//		mc->m_company2,mc->m_rpq,on,an,buf);
		//	boost = (double)an/(double)mc->m_rpq;
		//}

		if ( ! found && strstr ( buf , "raw=" ) ) {
			leeches++;
			if ( doFilter )	printf ("leech: %s",buf);
			// get the ip
			char *send = s; 
			while ( *send && *send != ' ') send++;
			char c = *send;
			*send = '\0';
			// convert ip string to long
			unsigned long ip ;
			struct in_addr in;
			in.s_addr = 0;
			inet_aton ( s , &in );
			ip = in.s_addr;
			*send = c;
			// hash into table
			long n = (ip % TABLE_SIZE);
			while ( buckets[n] && buckets[n] != ip ) 
				if ( ++n >= TABLE_SIZE ) n = 0;
			buckets[n] = ip;
			counts[n]++;
		}

		// add up the cost

	} // end loop over lines in log file
	fclose(fd);

	printf("Processed %li searches\n",totalSearches - nn );

	} // end loop over log files in dir

	// print leech ips
	if ( doFilter ) {
		// compress
		long p = 0;
		for ( long i = 0 ; i < TABLE_SIZE ; i++ ) {
			if ( ! counts[i] ) continue;
			buckets[p] = buckets[i];
			counts [p] = counts [i];
			p++;
		}
		// bubble sort the buckets by the counts field
		bool flag = true;
		while ( flag ) {
			flag = false;
			for ( long i = 1 ; i < p ; i++ ) {
				if ( counts[i] <= counts[i-1] ) continue;
				long tc = counts[i];
				long ti = buckets[i];
				counts [i] = counts [i-1];
				buckets[i] = buckets[i-1];
				counts [i-1] = tc;
				buckets[i-1] = ti;
				flag = true;
			}
		}
		// print the buckets out
		for ( long i = 0 ; i < p ; i++ ) {
			if ( ! buckets[i] ) continue;
			if ( ! counts [i] ) continue;
			long ip;
			
			printf("count=%li ip=%s\n", 
			       counts[i],iptoa(buckets[i]));
		}
		// all done
		return 0;
	}

	// get old month name and year
	char prevMonthName[32];
	char prevYearName [32];
	time_t t = time(NULL);
	t -= 20*24*3600 ;
	struct tm *timeStruct = localtime ( &t );
	strftime ( prevMonthName , 32 , "%b" , timeStruct );
	strftime ( prevYearName  , 32 , "%Y" , timeStruct );

	// get current month name
	char currentMonthName[32];
	char currentYearName [32];
	t += 20*24*3600 ;
	t += 5*24*3600 ;
	timeStruct = localtime ( &t );
	strftime ( currentMonthName , 32 , "%b" , timeStruct );
	strftime ( currentYearName  , 32 , "%Y" , timeStruct );

	// add up all money (non-late fees) owed to us this month
	double super = 0.0;

	char done[5000];
	memset ( done , 0 , 5000 );

	// make the emails -- store in files in the dir
	for ( long i = 0 ; i < nc ; i++ ) {

	// get the client
	Client *c = &s_clients[i];

	// skip if inactive
	//if ( c->m_active[0]=='N' || c->m_active[0]=='n' ) continue;

	// skip if already done
	//if ( done[i] ) continue;

	// consolidate searches and money from same named clients
	//for ( long j = i+1 ; j < nc ; j++ ) {
	//	if ( strcasecmp(s_clients[j].m_company,
	//			s_clients[i].m_company) != 0 ) continue;
	//	s_clients[i].m_searches += s_clients[j].m_searches;

	// compute total owed
	double total = 0;

	// convert from range to size
	// cinco put ranges in the table, not sizes that he should have
	//long size2 = c->m_cpmSize2 - c->m_cpmSize1;

	// add price from first tier
	//double ns = c->m_searches;
	//if ( ns > c->m_cpmSize1 ) ns = c->m_cpmSize1;
	//if ( ns > 0.0           ) total += (ns * c->m_cpm1) / 1000.0;
	// add price from second tier
	//ns = c->m_searches - c->m_cpmSize1;
	//if ( ns > c->m_cpmSize2 ) ns = c->m_cpmSize2;
	//if ( ns > size2 ) ns = size2;
	//if ( ns > 0.0   ) total += (ns * c->m_cpm2) / 1000.0;
	// add price from third tier
	//ns =c->m_searches - c->m_cpmSize2; // c->m_cpmSize1 - c->m_cpmSize2 ;
	//if ( ns > 0.0           ) total += (ns * c->m_cpm3) / 1000.0;

	// we now do an ongoing total above
	total = ((double)c->m_charge) / (1000.0*100.0);

	double minCharge = c->m_minCharge;
	// discount if they started mid month
	minCharge = (minCharge * (double)c->m_percent) / 100.0;

	// ensure we meet the monthly minimum charge
	if ( total < c->m_minCharge ) total = minCharge;

	// if they did less than 30000 searches (1k searches per day) no charge
	// but not jupitermedia! they are always $1000 per month min
	// because i build a custom index for them
	// let's try charging regardless, now
	//if ( c->m_searches < 30000 && c->m_minCharge < 400.00 ) total = 0;
	// . people with gigabits pay $400 monthly min, can do 20k searches/mo
	// . idealab has a $500 monthly min
	//if ( c->m_searches < 20000 && c->m_minCharge < 500.00 ) total = 0;
	// discount if they started mid month
	//total = (total * (double)c->m_percent) / 100.0;

	// licence fee
	double license = c->m_license;
	// pro-rate it
	license = (license * (double)c->m_percent) / 100.0;	
	// add in license fee for globalspec and newspaperarchive
	total += license;

	// round pennies down, add a bit so $1.399999 goes to $1.40
	total += .001;

	// truncate
	total = ((double)((long)(total * 100))) / 100.0;

	char active = 1;
	if ( c->m_active[0]=='N' || c->m_active[0]=='n' ) active = 0;

	// add in dump fee
	if ( active ) super += c->m_dumpFee;

	// do not charge this month if not active, but still send them
	// an email if they owe us.
	if ( ! active ) total = 0;

	// count to super total
	super += total;
	// add in labor to "super"
	//if ( active && c->m_hours > 0.00 ) {
	// SF/SL might not be active and we did labor for them
	if ( c->m_hours > 0.00 ) {
		double labor = c->m_hours * c->m_hourlyRate;
		if ( c->m_hourlyRate <= 0.0 ) {
			fprintf(stderr,"client has %.2f hours and no hourly rate. Please fix in "
				"spreadsheet and retry.",c->m_hours);
			return -1;
		}
		labor = ((double)((long)(labor * 100))) / 100.0;
		super += labor;
	}

	// due date
	char dueDate [ 64 ];
	t = time(NULL) + 24*3600*c->m_daysToPay;
	timeStruct = localtime ( &t );
	strftime ( dueDate , 64 , "%b %d, %Y" , timeStruct );
	// stats
	//printf("%s generated $%.2f",c->m_company,total);
	//if ( c->m_prevBalance )
	//	printf(" (owed %.2f)",c->m_prevBalance);
	//printf("\n");
	// print the email
	/*
	fprintf ( fd , 
		  "%s, \n"
		  "\n"
		  "I may have mistakenly sent you two invoices today. \n"
		  "The first one was real and the second, if you got it, "
		  "was a mistake.\n"
		  "\n"
		  "Thanks,\n"
		  "Matt\n" , c->m_name );
	goto skip;
	*/

	// store email into a file
	char efile[512];
	sprintf(efile,"%s/email.%s.%s.%s",
		dirname,prevYearName,prevMonthName,c->m_company2);
	FILE *fd = fopen ( efile , "w+" );
	if ( ! fd ) {
		fprintf(stderr,"open %s : %s\n",efile,strerror(errno));
		return -1;
	}

	// mail subject is the first line always
	fprintf ( fd , "%s\nGigablast Invoice for %s %s for %s\n\n",
		  c->m_email, prevMonthName , prevYearName , c->m_company );

	fprintf ( fd , 
		  "%s, \n"
		  "\n"
		  "Thank you for using Gigablast.\n"
		  "\n" ,
		  c->m_name );

	//if ( strstr ( c->m_name , "Knoblauch" ) ||
	//     strstr ( c->m_company , "ISEEK" )     )
	//	fprintf ( fd , 
	//		  "In regards to Purchase Order #6098.\n\n");

	//fprintf ( fd ,
	//	  "********** IMPORTANT ********\n"
	//	  "PLEASE disregard the previous invoice for this month "
	//	  "because there was an accounting error on our end.\n"
	//	  "\n\n");


	//fprintf ( fd ,
	//	  "********** IMPORTANT ********\n"
	//	  "Our address has changed to the following:\n"
	//	  "Gigablast, Inc.\n"
	//	  "5600 Wyoming Blvd. NE Suite 160\n"
	//	  "Albuquerque, New Mexico 87109\n"
	//	  "Please send checks there.\n"
	//	  "\n\n" );

	// important notice
	/*
	fprintf ( fd ,
		  "********** IMPORTANT ********\n"
		  "The IP of gigablast.com has changed from 207.114.174.29\n"
		  "to 64.62.168.40. If you are connecting to gigablast.com\n"
		  "you should not have to do anything, but if you were\n"
		  "connecting to the IP directly, you will have to change\n"
		  "it. The new index is larger, fresher and more complete\n"
		  "than the old one. It is also hosted at a larger and more\n"
		  "reliable data center.\n"
		  //"Please update your scripts if you need to. \n"
		  //"The old IP will be going away in 1-2 weeks.\n"
		  //"********** IMPORTANT ********\n"
		  "\n\n");
	*/

	// print search feed people warning
	if ( c->m_license <= 0.00 )
		  fprintf ( fd , 
		  "******** IMPORTANT ********\n"
		  "To access the search feed now you must insert the\n"
		  "following secret CGI parameter into your query URLs:\n"
		  "&code=%s\n"
		  "\n\n", 
		   c->m_code);

	fprintf ( fd , 
		  "Balance from Previous Invoice ..... $%.2f\n" ,
		  c->m_prevBalance );

	fprintf ( fd , 
		  "Payment recvd since last invoice .. $%.2f\n" ,
		  c->m_lastPayment );


	//fprintf ( fd , 
	//	  "Previous Balance ................ $%.2f\n" ,
	//	  c->m_prevBalance );

	double labor = 0.0;
	if ( c->m_hours > 0.00 ) {
		labor = c->m_hours * c->m_hourlyRate;
		if ( c->m_hourlyRate <= 0.0 ) {
			fprintf(stderr,"client has %.2f hours and no hourly rate. Please fix in "
				"spreadsheet and retry.",c->m_hours);
			return -1;
		}
		fprintf ( fd , 
			  "%s %s Labor .................... $%.2f (%.1f hours @ $%.2f/hour)\n",
			  prevMonthName ,
			  prevYearName  ,
			  labor , c->m_hours , c->m_hourlyRate );
	}


	// if we had a license fee
	if ( c->m_license > 0.00 && total > 0.0 ) {
		char *ss ="";
		if ( c->m_percent != 100 ) ss = " [prorated]";
		fprintf ( fd , 
			  "%s %s License/Service Fee ...... $%.2f%s\n",
			  prevMonthName ,
			  prevYearName  ,
			  total ,
			  ss );
	}
	else if ( total > 0.0 ) {
		// if we had no license
		fprintf ( fd , 
		"%s %s searches ................. $%.2f (for %li searches)\n",
			  prevMonthName ,
			  prevYearName  ,
			  total         ,
			  c->m_searches );
	}

	// if we had a data dump fee
	if ( active && c->m_dumpFee > 0.00 ) {
		fprintf ( fd , 
			  "%s %s Data Dump Fee ............ $%.2f\n",
			  prevMonthName ,
			  prevYearName  ,
			  c->m_dumpFee );
	}

	float interestable = c->m_prevBalance - c->m_lastPayment;
	if ( interestable <= 0 ) interestable = 0;
	// ask is on a 45-day pay period...
	if ( strcmp(c->m_code,"askste")== 0 ) interestable = 0.0;
	float interest = .015 * interestable;
	if ( interestable > 0.0 )
		fprintf ( fd ,
			  "Monthly Interest .................. $%.02f "
			  "(1.5%% of $%.02f)\n",
			  interest,interestable);

	// this month's bill/total
	c->m_bill = total + labor + interest;
	// set total for this client
	c->m_total = c->m_prevBalance - c->m_lastPayment + total + labor + 
		interest ;
	// only add this in if they are active
	if ( active ) c->m_total += c->m_dumpFee;
	// print totals owed and due date
	fprintf ( fd , 
		  "Total Owed ........................ $%.2f\n"
		  "Latest Due Date ................... %s\n"
		  "\n" ,
		  c->m_total ,
		  dueDate );

	// if they owe nothing say that
	if ( c->m_total <= 0 ) 
		fprintf ( fd , "** No Payment Required **\n\n" );


	// break down the searches
	if ( c->m_license == 0.00 )
		fprintf ( fd , "**** SEARCH BREAKDOWN ****\n" );
	if ( c->m_cachedPageRequests > 0 ) {
		double cost = ((double)c->m_cpms[0][0][0])/100.0;
		fprintf ( fd, "%li cached page requests. CPM=8x$%.4f=$%.4f "
			  "Cost=$%.2f\n\n",
			  (long)c->m_cachedPageRequests ,
			  // always use tier 0 for these i guess
			  cost,
			  8.0 * cost,
			  8.0 * cost * (double)c->m_cachedPageRequests/1000.0);
	}
			  
	for ( long w = 0 ; w < 2 ; w++ ) {
	for ( long i = 0 ; i < 2 ; i++ ) {
		for ( long j = 0 ; j < 3 ; j++ ) {
			for ( long k = 0 ; k < 8 ; k++ ) {
				long count = c->m_counts[w][i][j][k];
				if ( count == 0 ) continue;
				long an = 0;
				switch (k) {
				case 0: an = 10; break;
				case 1: an = 15; break;
				case 2: an = 20; break;
				case 3: an = 50; break;
				case 4: an = 100; break;
				case 5: an = 200; break;
				case 6: an = 500; break;
				case 7: an = 1000; break;
				}
				double cost ;
				cost = ((double)c->m_cpms[i][j][k])/100.0;
				long a = 0;
				long b = 0;
				if ( j == 0 ) {
					b = c->m_tierSize1;
				}
				if ( j == 1 ) {
					a = c->m_tierSize1;
					b = a + c->m_tierSize2;
				}
				if ( j == 2 ) {
					a = c->m_tierSize1 + c->m_tierSize2;
					b = 2000000000;
				}
				double charge;
				charge = (double)count * (double)cost / 1000.0;
				char *wg = "";
				if ( i == 1 ) wg = " with gigabits";
				if ( w == 1 )
				fprintf(fd,"Tier #%li "
					"(searches %li to %li) "
					"had\n"
					"%li n=%li "
					"searches%s with cts. "
					"CPM=$%.4fx4=$%.2f. "
					"Cost=$%.4fx4=$%.2f.\n\n",
					(j+1),
					a,b,
					count,
					an,
					wg,
					cost,
					cost*4,
					charge,
					charge*4);
				else
				fprintf(fd,"Tier #%li "
					"(searches %li to %li) "
					"had\n"
					"%li n=%li "
					"searches%s. "
					"CPM=$%.4f. "
					"Cost=$%.4f.\n\n",
					(j+1),
					a,b,
					count,
					an,
					wg,
					cost,
					charge);
			}
		}
	}
	}

	// partner breakdown
	if ( strcmp(c->m_code,"askste")== 0 ) {
		fprintf(fd,"\n**** Partner Query Breakdown ****\n");
		long ptotal = 0;
		for ( long i = 0 ; i < 1024 ; i++ ) {
			if ( !pslots[i] ) continue;
			// print it otherwise
			fprintf(fd,"%s .. %li queries\n",pslots[i],pcounts[i]);
			ptotal += pcounts[i];
		}
		fprintf(fd,"Total Partner Queries: %li\n",ptotal);
		fprintf(fd,"\n");
	}

	if ( c->m_minCharge > 0.00 &&
	     (c->m_active[0]=='Y' || c->m_active[0]=='y' ) ) 
		fprintf(fd,"Minimum monthly fee: $%.2f.\n",c->m_minCharge);

	if ( c->m_percent != 100 && 
	     (c->m_active[0]=='Y' || c->m_active[0]=='y' ) ) {
		fprintf(fd,"Percent monthly usage: %li%%\n",c->m_percent);
		fprintf(fd,"Pro-rated minimum monthly fee: $%.2f.\n",
			(double)c->m_minCharge*(double)c->m_percent/100.0);
	}

	if ( c->m_startDate[0] ) 
	      fprintf(fd,"Start date: %s\n",c->m_startDate);

	if ( c->m_termNotice[0] ) {
	      fprintf(fd,"Received termination notice: %s\n",c->m_termNotice);
	      fprintf(fd,"Termination effective: %s\n",c->m_termEffective);
	}


	fprintf(fd,"\n\n");

	

	// print search feed people warning
	//if ( c->m_license <= 0.00 )
	//	fprintf ( fd , 
	//		  "Please let us know if you are feeding from any IP "
	//		  "address not in the following list : %s\n"
	//		  "\n" , c->m_ips);

	// print where to send the money
	fprintf ( fd , 
		  "You can either wire the money to this bank account:\n"
		  "\n"
		  "Gigablast, Inc.\n"
		  "Checking Account #004275336550\n"
		  "Routing (ABA) #026009593\n"
		  "Bank of America located at 8040 Academy Road NE,\n"
		  "Albuquerque, New Mexico 87111 U.S.A.\n"

		  //"Matt Wells LLC\n"
		  //"Checking Account #004271977418\n"
		  //"Routing (ABA) #107000327\n"
		  //"Bank Of America located at 2011 Juan Tabo Blvd N.E.,\n"
		  //"Albuquerque, New Mexico 87112\n"
		  "\n"
		  "Or send a check payable to Gigablast, Inc. to:\n"
		  "\n"
		  "Gigablast, Inc.\n"
		  //"5600 Wyoming Blvd. NE Suite 160\n"
		  "4001 Bogan NE. Ave. Bldg A\n"
		  "Albuquerque, New Mexico 87109\n"
		  "USA\n"
		  "\n"
		  "Thanks,\n"
		  //"Matt Wells\n"
		  "The Gigablast Team\n"
		  "email: gigablast@mail.com\n"
		  //"(505) 797-3913 x100 (voice)\n"
		  //"(505) 212-0310 (fax)\n"
		  //"Gigablast.com\n"
		  "\n");
	//skip:
	fclose(fd);
	// if they don't owe anything, do not print the email
	if ( c->m_total == 0.00 && c->m_bill == 0.00 ) unlink(efile);

	}

	// print out the stats, store in file
	char sumfile[512];
	sprintf(sumfile,"%s/SUMMARY",dirname);
	FILE *fd = fopen ( sumfile , "w+" );
	if ( ! fd ) {
		fprintf(stderr,"open %s : %s\n",sumfile,strerror(errno));
		return -1;
	}
	float sum = 0.0;
	// print each client in the summary
	for ( long i = 0 ; i < nc ; i++ ) {
		// print a line about the client
		Client *c = &s_clients[i];
		// limit each client name
		char nn[1024];
		sprintf ( nn , "%s",c->m_company );
		long nnlen = strlen(nn);
		while ( nnlen < 12 ) nn[nnlen++] = ' ';
		nnlen = 12; nn[nnlen]='\0';
		//if ( c->m_searches )
		// inactives all have 0 for total, just make it the prevBal
		//if ( c->m_active[0]=='N' || c->m_active[0]=='n' )
		//	c->m_total = c->m_prevBalance;
		sum += c->m_bill;
		fprintf(fd,"$% 10.2f %s (searches=%8li) "
			"(total owed=$% 10.2f)",
			c->m_bill,//c->m_total, // -c->m_prevBalance,
			nn,c->m_searches,c->m_total);
			//else
			//	fprintf(fd,"$% 9.2f %s\n",c->m_total,nn);
			char pmu = 0;
			if ( c->m_percent != 100 ) pmu = 1;
			if ( c->m_active[0]=='N' || c->m_active[0]=='n' )
				pmu = 0;
			if ( pmu )
				fprintf(fd," [%li%%]",c->m_percent);
			// skip if inactive
			if ( c->m_active[0]=='N' || c->m_active[0]=='n' )
				fprintf(fd," [INACTIVE]");

			fprintf(fd,"\n");
	}
	// wrap it up
	fprintf(fd,"There were %li total searches.\n",totalSearches);
	fprintf(fd,"There were %li raw scrapes.\n",leeches);
	fprintf(fd,"Total money generated from %s %s = $%.2f\n",
		prevMonthName,prevYearName,sum);//super);
	fclose(fd);
}

char *iptoa ( long ip ) {
	static char s_buf [ 32 ];
	sprintf ( s_buf , "%hhu.%hhu.%hhu.%hhu",
		  (unsigned char)(ip >>  0)&0xff,
		  (unsigned char)(ip >>  8)&0xff,
		  (unsigned char)(ip >> 16)&0xff,
		  (unsigned char)(ip >> 24)&0xff);
	return s_buf;
	//struct in_addr in;
	//in.s_addr = ip;
	//return inet_ntoa ( in );
}

// independent of case
char *strcasestr2 ( char *haystack , char *needle ) {
	long needleSize   = strlen(needle);
	long haystackSize = strlen(haystack);
	long n = haystackSize - needleSize ;
	for ( long i = 0 ; i <= n ; i++ ) {
		// keep looping if first chars do not match
		if ( tolower(haystack[i]) != tolower(needle[0]) ) continue;
		// if needle was only 1 char it's a match
		if ( ! needle[1] ) return &haystack[i];
		// compare the whole strings now
		if ( strncasecmp ( &haystack[i] , needle , needleSize ) == 0 ) 
			return &haystack[i];			
	}
	return NULL;
}



