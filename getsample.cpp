/* Standalone program that takes the query data (queries performed
   by various clients and people) from the logs and filters it for download by
   our data license clients. The specs of the program are as follows:

   1. syntax: 
   getsample <directoryContainingTheLogFiles> <outputDirectory> <monthdayhour> 
   2. month day and hour are each 2 digits (zero padded)
   3. the output of getqueries will be stored in a file called
   sample.monthdayhour in the output directory, where month, day and hour are 
   2 digits each. like sample.020312. that sample should then be compressed
   using a system() call to bzip2 to make it sample.020312.bz2. typically the 
   output directory will be /a/html/ so our clients can download the samples
   over our http server.
   4. "getsample <dirOfLogs> <outputDir> lasthour" will use the hour before the
   current hour so we can do dumps every hour called by a cron job. lasthour is
   the actual string "lasthour" not a number. getsample has to look at the
   current time and use the previous hour for this value.
   5. see opendir() invoicer.cpp for code that scans the files in a given
   directory. we need to scan all files starting with "log" to get the sample
   data, just like in invoicer.cpp. all log files (except the current one) 
   have a date appended to their names. Use that date to avoid scanning them 
   for queries if the date is before the requested monthdayhour. that date 
   is when the next log file was opened and they were set aside.
   6. all ip addresses in the 8th column of the sample should be consistently
   remapped by using hash32() (steal from hash.cpp). 
   7. all ip addresses that are values for the &uip= cgi parm should likewise 
   be remapped.
   8. all &code= cgi values should be remapped by hashing their current value
   (an ascii string) to a 32-bit number using hash32(). the string should be
   replaced with that number. so the viewer knows they are coming from the
   same client, but the client's actual passcode is not known.
   9. all dates are in GMT (UTC). the timestamps in the log are already in UTC.
   10. getsample should probably compile independently of the gb source code if
   possible.
*/

#include "gb-include.h"

#include <errno.h>
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

//lets not read more than 10mb at a time
#define MAX_READ_SIZE 10*1024*1024

class GetSample {
public:
	char   m_year[3];
	char   m_month[3] ;
	char   m_day[3]    ;
	char   m_hour[3];
};

static char *s_month[] = {
	{"XXX"},{"Jan"},{"Feb"},{"Mar"},{"Apr"},{"May"},{"Jun"},
	{"Jul"},{"Aug"},{"Sep"},{"Oct"},{"Nov"},{"Dec"}
};

//Defining getSample as global
class GetSample g_getSample;

uint64_t g_hashtab[256][256] ;

bool hashinit () {
	static bool s_initialized = false;
	// bail if we already called this
	if ( s_initialized ) return true;
	// show RAND_MAX
	//printf("RAND_MAX = %"UINT32"\n", RAND_MAX ); it's 0x7fffffff
	// seed with same value so we get same rand sequence for all
	srand ( 1945687 );
	for ( int32_t i = 0 ; i < 256 ; i++ )
		for ( int32_t j = 0 ; j < 256 ; j++ ) {
			g_hashtab [i][j]  = (uint64_t)rand();
			// the top bit never gets set, so fix
			if ( rand() > (0x7fffffff / 2) ) 
				g_hashtab[i][j] |= 0x80000000;
			g_hashtab [i][j] <<= 32;
			g_hashtab [i][j] |= (uint64_t)rand();
			// the top bit never gets set, so fix
			if ( rand() > (0x7fffffff / 2) ) 
				g_hashtab[i][j] |= 0x80000000;
		}
	//if ( g_hashtab[0][0] != 6720717044602784129LL ) return false;
	s_initialized = true;
	return true;
}

uint32_t hash32 ( const char *s, int32_t len ) {
	uint32_t h = 0;
	int32_t i = 0;
	while ( i < len ) {
		h ^= (uint32_t) g_hashtab [(unsigned char)i]
			[(unsigned char)s[i]];
		i++;
	}
	return h;
}

bool processLog(FILE *fdip, FILE *fdop){
	//some urls are very int32_t, so keeping 10k
	char line[10*1024];
	//buffer to store
	char buf[10*1024];
	char *p=line;
	int32_t i=0;
	while(fgets(line,10*1024,fdip)){
		char *q=buf;
		char *lineStart=p;
		char *ipStart=p;
		char *urlStart=strstr(p," GET /search");
		if (!urlStart)
			urlStart=strstr(p," POST /search");
		if(!urlStart)
			continue;
		
		//don't check for errors, grep could do that
		/*char *tmp=strstr(p," (error: ");
		if (tmp)
		continue;*/


		//we reached a GET or POST line in the log
		// have to check the month, day and hour
		//get to the date
		ipStart+=32;
		// check if we have the right month
		if (strncasecmp(g_getSample.m_month,"xx",2)!=0 &&
		    strncasecmp(ipStart,
				 s_month[atoi(g_getSample.m_month)],3) )
			continue;
		//if we do not have the right month, do you think we should
		// just skip this file? Not now, maybe would write the code
		// later to read the next file here and check if we've skipped 
		// a lot.
		ipStart+=4; //skipping the month
		// check if we have the right day
		if (strncasecmp(g_getSample.m_day,"xx",2)!=0 &&
		    strncasecmp(ipStart,g_getSample.m_day,2) )
			continue;
		
		ipStart += 3;
		//check if we are at the right hour
		if (strncasecmp(g_getSample.m_hour,"xx",2)!=0 &&
		    strncasecmp(ipStart,g_getSample.m_hour,2) ){
			continue;
		}
		//we found a match
		//null end after the query, we don't need anything after that
		//was getting a seg fault because some line in tha log did not
		// have HTTP at the end, which is weird. Just take \n
		char *end=strstr(line,"\n");
		//if a query is more than 10k, just trunc it at 10k
		if (!end)
			line[10*1024-1]='\0';
		else
			end[0]='\0';

		// go to ip of the user
		ipStart+=9;
		//copy everything before that
		strncpy(q,lineStart,ipStart-lineStart);
		q+=ipStart-lineStart;
		char ip[18];
		strncpy(ip,ipStart,urlStart-ipStart);
		ip[urlStart-ipStart]='\0';
	       
		uint32_t ipHash=hash32(ip,gbstrlen(ip));
		sprintf(q,"%u.%u.%u.%u \0",(unsigned char)(ipHash>>24),
			(unsigned char)(ipHash>>16),
			(unsigned char)(ipHash>>8),
			(unsigned char)ipHash);
		q+=gbstrlen(q);
		//		urlStart+=12;
		
		char *codeStart= strstr(urlStart,"code=");
		char *uipStart=	strstr(urlStart,"uip=");

		if (!codeStart && !uipStart){
			sprintf(q,"%s\n\0",urlStart);
			q+=gbstrlen(q);
		}
		else if ((codeStart && codeStart<uipStart) || !uipStart){
			codeStart+=5;
			strncpy(q,urlStart,codeStart-urlStart);
			q[codeStart-urlStart]='\0';
			q+=gbstrlen(q);
			//I've seen some bad codes being junk and > 50 chars
			char code[50];
			int32_t i=0;
			while (codeStart[i]!='\0' && codeStart[i]!='&' && 
				     codeStart[i]!=' ' && i<50){
				code[i]=codeStart[i];
				i++;
			}
			code[i]='\0';
			codeStart+=i;
			uint32_t codeHash=hash32(code,gbstrlen(code));
			sprintf(q,"%u\0",codeHash);
			q+=gbstrlen(q);
			
			if (uipStart){
				uipStart+=4;
				strncpy(q,codeStart,uipStart-codeStart);
				q[uipStart-codeStart]='\0';
				q+=gbstrlen(q);
				char uip[50];
				int32_t j=0;
				while(uipStart[j]!='\0' && 
				      uipStart[j]!='&' &&
				      uipStart[j]!=' ' && j<50){
					uip[j]=uipStart[j];
					j++;
				}
				uip[j]='\0';
				uipStart+=j;
				uint32_t uipHash=hash32(uip,gbstrlen(uip));
				sprintf(q,"%u.%u.%u.%u\0",
					(unsigned char)(uipHash>>24),
					(unsigned char)(uipHash>>16),
					(unsigned char)(uipHash>>8),
					(unsigned char)uipHash);
				q+=gbstrlen(q);
				//copy everything after that, and null end q
				sprintf(q,"%s\n\0",uipStart);
				/*strncpy(q,uipStart,gbstrlen(uipStart));
				  q+=gbstrlen(uipStart);*/
			}
			else{
				/*strncpy(q,codeStart,gbstrlen(codeStart));
				  q+=gbstrlen(codeStart);*/
				sprintf(q,"%s\n\0",codeStart);
			}
		}
		else if((uipStart && uipStart<codeStart) || !codeStart){
			uipStart+=4;
			strncpy(q,urlStart,uipStart-urlStart);
			q[uipStart-urlStart]='\0';
			q+=gbstrlen(q);
			char uip[50];
			int32_t i=0;
			while(uipStart[i]!='\0' && 
			      uipStart[i]!='&' &&
			      uipStart[i]!=' ' && i<50){
				uip[i]=uipStart[i];
				i++;
			}
			uip[i]='\0';
			uipStart+=i;
			uint32_t uipHash=hash32(uip,gbstrlen(uip));
			sprintf(q,"%u.%u.%u.%u\0",
				(unsigned char)(uipHash>>24),
				(unsigned char)(uipHash>>16),
				(unsigned char)(uipHash>>8),
				(unsigned char)uipHash);
			q+=gbstrlen(q);
			
			if (codeStart){
				codeStart+=5;
				strncpy(q,uipStart,codeStart-uipStart);
				q[codeStart-uipStart]='\0';
				q+=gbstrlen(q);
				char code[50];
				int32_t j=0;
				while (codeStart[j]!='\0' && 
				       codeStart[j]!='&' && 
				       codeStart[j]!=' ' && j<50){
					code[j]=codeStart[j];
					j++;
				}
				code[j]='\0';
				codeStart+=j;
				uint32_t codeHash=hash32(code,
							      gbstrlen(code));
				sprintf(q,"%u\0",codeHash);
				q+=gbstrlen(q);
				//copy everything after that
				sprintf(q,"%s\n\0",codeStart);
				/*strncpy(q,codeStart,gbstrlen(codeStart));
				  q+=gbstrlen(codeStart);*/
			}
			else{
				sprintf(q,"%s\n\0",uipStart);
				/*strncpy(q,uipStart,gbstrlen(uipStart));
				  q+=gbstrlen(uipStart);*/
			}
		}
		i++;		
		//		fprintf(stderr,"%s",buf);
		fputs(buf,fdop);
	}
	if (i>0)
		fprintf(stderr,"Found %u queries\n",i);
}

int main ( int argc , char *argv[] ) {
	FILE *fdip,*fdop;
	char fileip[1024],fileop[1024];
	// first arg is the directory of log* files
	// second is the output dir
	// third is the mmddhh or is the string lasthour
	if ( argc < 2 ) {
usage:
		fprintf(stderr,"Usage: getsample [OPTION]... DATE \n");
		fprintf(stderr,"Output the queries from a gb log file"
			" of a particular date \n"
			"Eg. getsample -i /usr 06050302 \n");
		fprintf(stderr,"OPTION:\n"
			"-i        Input is a directory containing log files\n"
			"-o        Output to a directory with filename"
			"sample.DATE\n\n\n");

		fprintf(stderr,"\tDATE is in the form of yymmddhh, where "
			"each are 2 digits "
			"(zero padded) and can be skipped "
			"by putting 'xx' in place of them " 
			"eg. 06020312, 03xx04xx, 04xxxxxx, xx08xx. \n");
		
		fprintf(stderr,"\tDATE can be replaced by the string "
			"'lasthour' which dumps the queries "
			"in the hour before the current hour\n");
		return -1;
	}

	int32_t ipArg=0;
	int32_t opArg=0;
	int32_t i=1;
	while (i < argc){
		if(strncmp(argv[i],"-i",2)==0)
			ipArg=i+1;
		else if(strncmp(argv[i],"-o",2)==0)
			opArg=i+1;
		i++;
	}
	if (ipArg==0)
		fdip=stdin;
	if (opArg==0)
		fdop=stdout;

			
	//else cycle through 
	//check if it is mmddhh or lasthour
	if (strcmp(argv[argc-1],"lasthour")==0){
		//get the current time
		time_t rawTime;
		struct tm *timeInfo;
		time (&rawTime);
		//Reduce rawTime by 1 hour(3600 secs)
		rawTime-=3600;
		//timeInfo stores the lasthour UTC time
		timeInfo=localtime(&rawTime);
		int32_t year = timeInfo->tm_year;
		if (year > 100 )
			year -= 100;
		sprintf(g_getSample.m_hour,"%02"INT32"",timeInfo->tm_hour);
		sprintf(g_getSample.m_day,"%02"INT32"",timeInfo->tm_mday);
		sprintf(g_getSample.m_month,"%02"INT32"",timeInfo->tm_mon+1);
		sprintf(g_getSample.m_year,"%02"INT32"",year);
	}
	else{
		if (gbstrlen(argv[argc-1]) != 8){
			fprintf(stderr,"yymmddhh are each 2 digits "
				"(zero padded) and can be skipped "
				"by putt 'xx' in place of them " 
				"eg. xx020312, 03xx04xx, 04xxxxxx. \n");
			return -1;
		}
		//put the yymmddhh string into different vars
		strncpy(g_getSample.m_year,argv[argc-1],2);
		strncpy(g_getSample.m_month,argv[argc-1]+2,2);
		strncpy(g_getSample.m_day,argv[argc-1]+4,2);
		strncpy(g_getSample.m_hour,argv[argc-1]+6,2);
		//null end;
		g_getSample.m_year[2]='\0';
		g_getSample.m_month[2]='\0';
		g_getSample.m_day[2]='\0';
		g_getSample.m_hour[2]='\0';
	}
	
	//would be good to print the date on stderr
	fprintf(stderr,"year=%s, month=%s, day=%s, hour=%s \n",
		g_getSample.m_year,
		g_getSample.m_month,
		g_getSample.m_day,
		g_getSample.m_hour);		

	//if we have an output dir given
	if(opArg>0){
		sprintf(fileop,"%s/sample.%s%s%s%s\0",argv[opArg],
			g_getSample.m_year,
			g_getSample.m_month,
			g_getSample.m_day,
			g_getSample.m_hour);
		fdop=fopen(fileop,"w+");
		if (!fdop){
			fprintf(stderr,"getSample::open %s : %s\n",
				fileop,strerror(errno));
			return false;
		}
	}

	if ( ! hashinit () ) return 0;

	if (ipArg==0){
		processLog(fdip,fdop);
	}
	else{
		// open the dir and scan for log files
		DIR *edir = opendir (argv[1] );
		if ( ! edir ) {
			fprintf ( stderr, "getSample::opendir (%s):%s\n",
				  argv[1],strerror( errno ) );
			return -1;
		}

		// loop over all the log files in this directory
		struct dirent *ent;
		while ( (ent = readdir ( edir ))  ) {
			char *filename = ent->d_name;
			if ( strncasecmp ( filename , "log" , 3 ) != 0 ) 
				continue;
			// skip if ends in a ~, it is an emacs backup file
			if ( filename[gbstrlen(filename)-1] == '~' ) continue;
			//no use processing if the log file date is older than
			// the start date given to us underscore is before
			// the month
			/*			char *p=strstr(filename,"_");
			//if it is a current log file, eg. log0, then do it
			if (p){
				p++;
				int32_t m,d,h;
				m=atoi(p);
				p+=3;
				d=atoi(p);
				p+=3;
				h=atoi(p);
				if (m<atoi(g_getSample.m_month))
					continue;
				if (m==atoi(g_getSample.m_month) &&
				    d<atoi(g_getSample.m_day))
					continue;
				p+=3;
				if (m==atoi(g_getSample.m_month) &&
				    d==atoi(g_getSample.m_day) &&
				    h<atoi(g_getSample.m_hour))
					continue;
					}*/

			// make a full filename
			sprintf ( fileip , "%s/%s", argv[1],filename);
			fprintf(stderr, "getSample::opening log file %s \n",
				fileip);
			// . returns -1 on failure
			fdip=fopen(fileip,"r");
			if (!fdip){
				fprintf(stderr,"getSample::open %s : %s\n",
					fileip,strerror(errno));
				return false;
			}
			// . we got one, process it
			processLog(fdip,fdop);
			//after processing close it
			fclose(fdip);
		}
	}
	//if not writing to stdout, close the file and bzip it
	if (fdop!=stdout){
		fclose(fdop);
		//do a system call to bzip the file
		char tmp[1024];
		sprintf(tmp,"gzip %s",fileop);
		system(tmp);
	}
	return 1;
}

