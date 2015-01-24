#undef _XOPEN_SOURCE
#define _XOPEN_SOURCE 500
#include "gb-include.h"
//#include "strings.h"

#include "Wiktionary.h"

#include "Query.h"
#include "Words.h"
#include "Titledb.h"
#include "Speller.h"

// the global instance
Wiktionary g_wiktionary;

Wiktionary::Wiktionary () {
	m_callback = NULL;
	m_state    = NULL;
	m_opened   = false;
	// . use a 8 byte key size and 2 byte data size
	// . allowDups = true!
	// . now m_langTable just maps to langId, no POS bits...
	//m_langTable.set ( 6 , 1,0,NULL,0,false,0 ,"wkt-lang"); 
	m_synTable.set  ( 6 , 4,0,NULL,0,true,0 ,"wkt-synt"); 

	m_synBuf.setLabel("synbuf");
}

void Wiktionary::reset() {
	//m_langTable.reset();
	m_synTable .reset();
	m_synBuf.purge();

	m_debugMap .reset();
	m_debugBuf .purge();

	m_dedup.reset();
	m_tmp.reset();

	m_langBuf.reset();

	m_localBuf.purge();
	m_localTable.reset();
}

Wiktionary::~Wiktionary () {
	if ( m_opened ) m_f.close();
}


bool Wiktionary::test ( ) {

	// test words parsing here
	//Words w;
	//w.set9 ("get $4,500.00 now",0);

	// test it out!
	char *str = "love";//pie"; //forsake";
	//int64_t wid = hash64Lower_utf8(str);
	int64_t wid = hash64n(str);
	// use this now
	char *p = getSynSet ( wid, langEnglish );
	//char *p = (char *)m_synTable.getValue ( &wid );
	// must be there
	if ( ! p ) { char *xx=NULL;*xx=0; }
	// first # is number of forms
	//if ( *p < 0 || *p > 100 ) { char *xx =NULL;*xx=0; }
	// first is count!
	//int32_t n = *p;
	// skip that
	//p++;
	// find new line
	char *end = p;
	for ( ; *end && *end !='\n' ; end++ );
	// tmp set
	*end = '\0';
	// cast it
	// only the first 6 bytes are valid
	//int64_t *termIds = (int64_t *)p;
	// header
	log("wikt: test \"%s\" -> \"%s\"",str,p);
	// back
	*end = '\n';

	return true;

	p = NULL;

	uint8_t langId = langEnglish;

 loop:
	char input[256];
	fgets(input,200,stdin);
	input[strlen(input)-1]='\0';
	if ( input[0] == '\0' ) return true;

	// get language
	char *pipe = strstr ( input, "|" );
	if ( ! pipe ) {
		fprintf(stderr,"lang = %s\n",getLangAbbr(langId));
		str = input;
	}
	else {
		*pipe = '\0';
		langId = getLangIdFromAbbr ( input );
		fprintf(stderr,"lang = %s\n",getLangAbbr(langId));
		str = pipe + 1;
	}
	//wid = hash64Lower_utf8(str);
	wid = hash64n(str);
	// use this now.
	p = getSynSet  ( wid, langId );//, WF_NOUN );
	// must be there
	if ( ! p ) {
		fprintf(stderr,"no forms\n"); 
		goto loop;
	}


	// find new line
	end = p;
	for ( ; *end && *end !='\n' ; end++ );
	// tmp set
	*end = '\0';
	// header
	fprintf(stderr,"%s\n",p);
	// back
	*end = '\n';

 again:
	p = getNextSynSet  ( wid, langId , p );
	if ( p ) {
		// find new line
		end = p;
		for ( ; *end && *end !='\n' ; end++ );
		// tmp set
		*end = '\0';
		// header
		fprintf(stderr,"%s\n",p);
		// back
		*end = '\n';
		// loop up
		goto again;
	}

	goto loop;

	return true;
}

#include "Synonyms.h"

bool Wiktionary::test2 ( ) {

 loop2:

	uint8_t langId = langEnglish; // langUnknown

	char input[256];
	fgets(input,200,stdin);
	input[strlen(input)-1]='\0';
	if ( input[0] == '\0' ) return true;

	char *str;

	// get language
	char *pipe = strstr ( input, "|" );
	if ( ! pipe ) {
		fprintf(stderr,"lang = %s\n",getLangAbbr(langId));
		str = input;
	}
	else {
		*pipe = '\0';
		langId = getLangIdFromAbbr ( input );
		fprintf(stderr,"lang = %s\n",getLangAbbr(langId));
		str = pipe + 1;
	}
	//wid = hash64Lower_utf8(str);
	//wid = hash64n(str);

	Words words;
	words.set3 ( str );
	int32_t wordNum = 0;
	char tmpBuf[1000];
	int32_t niceness = 0;
	Synonyms syn;
	int32_t naids = syn.getSynonyms ( &words,
				       wordNum , 
				       langId ,
				       tmpBuf ,
				       niceness );
	// print those out
	SafeBuf sb;
	for ( int32_t k = 0 ; k < naids ; k++ ) {
		char *str = syn.m_termPtrs[k];
		int32_t  len = syn.m_termLens[k];
		sb.safeMemcpy(str,len);
		if ( k+1<naids) sb.pushChar(',');
	}
	sb.pushChar('\0');

	// use this now.
	//p = getSynSet  ( wid, langId );//, WF_NOUN );

	// must be there
	if ( ! naids ) {
		fprintf(stderr,"no forms\n"); 
		goto loop2;
	}

	fprintf(stderr,"%s -> %s\n",str,sb.getBufStart());
	goto loop2;
}

// . load from disk
bool Wiktionary::load() {

	// load it from .dat file if exists and is newer
	char ff1[256];
	//char ff2[256];
	char ff3[256];
	char ff4[256];
	sprintf(ff1, "%swiktionary.txt.aa", g_hostdb.m_dir);
	//sprintf(ff2, "%swiktionary-mybuf.txt", g_hostdb.m_dir);
	sprintf(ff3, "%swiktionary-syns.dat", g_hostdb.m_dir);
	sprintf(ff4, "%swiktionary-buf.txt", g_hostdb.m_dir);
	//FILE *fd1b = fopen ( ff1, "r" );
	//if ( ! fd1b ) {
	//	fprintf(stderr,"fopen: %s",mstrerror(errno));
	//	return false;
	//}
	int fd1 = open ( ff1 , O_RDONLY );
	//int fd2 = open ( ff2 , O_RDONLY );
	int fd3 = open ( ff3 , O_RDONLY );
	if ( fd3 < 0 ) log(LOG_INFO,"wikt: open %s: %s",ff3,mstrerror(errno));
	int fd4 = open ( ff4 , O_RDONLY );
	if ( fd4 < 0 ) log(LOG_INFO,"wikt: open %s: %s",ff1,mstrerror(errno));
	struct stat stats1;
	//struct stat stats2;
	struct stat stats3;
	struct stat stats4;
	int32_t errno1 = 0;
	//int32_t errno2 = 0;
	int32_t errno3 = 0;
	int32_t errno4 = 0;
	if ( fstat ( fd1 , &stats1 ) == -1 ) errno1 = errno;
	//if ( fstat ( fd2 , &stats2 ) == -1 ) errno1 = errno;
	if ( fstat ( fd3 , &stats3 ) == -1 ) errno3 = errno;
	if ( fstat ( fd4 , &stats4 ) == -1 ) errno4 = errno;
	// close all
	close ( fd1 );
	//close ( fd2 );
	close ( fd3 );
	close ( fd4 );
	// save text size for getRandomPhrase() function below
	//m_txtSize = stats1.st_size;
	// if we got a newer binary version, use that
	if ( ! errno3 && ! errno4 && 
	     // load from binaries if orig txt is not there OR our
	     // binary make time is ahead of the orig txt make time
	     ( errno1 || stats3.st_mtime > stats1.st_mtime ) 
	     //&& ( errno2 || stats3.st_mtime > stats2.st_mtime ) 
	     ) {
		log(LOG_INFO,"wikt: Loading %s",ff3);
		if ( ! m_synTable .load ( NULL , ff3 ) )
			return false;
		log(LOG_INFO,"wikt: Loading %s",ff4);
		if ( m_synBuf.fillFromFile ( NULL , ff4 ) <= 0 )
			return false;

		// augment wiktionary with our own overrides and additions from
		if ( ! addSynsets ( "mysynonyms.txt" ) ) 
			return false;

		// a quick little checksum
		if ( ! g_conf.m_isLive ) return true;

		// the size
		int64_t h1 = m_synTable.getNumSlotsUsed();
		int64_t h2 = m_synBuf  .length();
		int64_t h = hash64 ( h1 , h2 );
		char *tail1 = (char *)m_synTable.m_keys;
		char *tail2 = m_synBuf  .getBufStart()+h2-1000;
		h = hash64 ( tail1 , 1000 , h );
		h = hash64 ( tail2 , 1000 , h );
		int64_t nn = -662959013613045013LL;//-6197041242898026762LL;
		int64_t nn2 = -2511412928924361809LL;
		if ( h != nn && h != nn2 ) {
			log("gb: %s or %s checksum is not approved for "
			    "live service (%"INT64" != %"INT64")", ff3, ff4,
			    h,nn);
			//return false;
		}

		return true;
	}
	// if no text file that is bad
	if ( errno1 ) { 
		g_errno = errno1 ; 
		return log ("gb: could not open %s for reading: %s",ff1,
			    mstrerror(g_errno));
	}
	//if ( errno2 ) { 
	//	g_errno = errno2 ; 
	//	return log ("gb: could not open %s for reading: %s",ff2,
	//		    mstrerror(g_errno));
	//}
	// init table slot sizes
	//m_langTable.setTableSize ( 16777216 , NULL , 0 );
	//m_synTable .setTableSize ( 16777216 , NULL , 0 );
	//m_debugMap .setTableSize ( 8388608  , NULL , 0 );
	m_dedup.set    ( 8 , 0 , 16777216 , NULL , 0 , false, 0,"ddtab");
	// this has to allow dups! it maps a baseForm to a variant/syn
	// now it includes langid
	m_tmp.set      ( 8 , 9 , 16777216 , NULL , 0 , true , 0,"tmptab");
	m_debugMap.set  ( 8 , 4,0,NULL,0,false,0 ,"wkt-dmap"); 
	//m_langTableTmp.set( 6 , 1,0,NULL,0,false,0 ,"wktlangt"); 
	// this maps a pure word id (wid) to an offset in m_debugBuf for
	// printing out the word
	//m_debugMap.set ( 6 , 4 , 8388608  , NULL , 0 , false, 0,"dbgmap");

	// get the size of it
	int32_t size = stats1.st_size;
	// now we have to load the text file
	// returns false and sets g_errno on error
	if ( ! generateHashTableFromWiktionaryTxt ( size ) ) return false;
	// success!
	return true;
}

static char *s_lowerLangWikiStrings[] = {
	"unknown","english","french","spanish","russian","turkish","japanese",
	"cantonese", // "chinese traditional",
	"mandarin", // "chinese simplified",
	"korean","german","dutch",
	"italian","finnish","swedish","norwegian","portuguese","vietnamese",
	"arabic","hebrew","indonesian","greek","thai","hindi","bengala",
	"polish","tagalog",

	"latin",
	"esperanto",
	"catalan",
	"bulgarian",
	"translingual",
	"serbo-croatian",
	"hungarian",
	"danish",
	"lithuanian",
	"czech",
	"galician",
	"georgian",
	"scottish gaelic",
	"gothic",
	"romanian",
	"irish",
	"latvian",
	"armenian",
	"icelandic",
	"ancient greek",
	"manx",
	"ido",
	"persian",
	"telugu",
	"venetian",
	"malagasy",
	"kurdish",
	"luxembourgish",
	"estonian"
};

// add our special augmentation table
// Synonyms.cpp should check this table separately so we can keep it
// somewhat small and re-load it on the fly.
// mysynonyms.txt
bool Wiktionary::addSynsets ( char *filename ) {

	// load it up
	//SafeBuf sb;
	if ( m_localBuf.fillFromFile ( g_hostdb.m_dir , filename ) < 0 ) 
		// log it
		return log("wikt: error loading %s",filename);

	if ( ! m_localTable.set ( 8 ,4,9000,NULL,0,false,0,"synloc") )
		return false;

	char *p = m_localBuf.getBufStart();

 nextLine:
	// get end of line
	char *eol = p;
	// sanity
	char *bufEnd = m_localBuf.getBuf();
	if ( eol >= bufEnd ) 
		return true;
	for ( ; *eol && *eol != '\n' ; eol++ );
	// skip spaces
	for ( ; *p == ' ' || *p == '\t' ; p++ );
	// skip comment lines
	if ( *p == '#' ) {
		p = eol + 1;
		goto nextLine;
	}
	// blank line?
	if ( *p == '\n' ) {
		p = eol + 1;
		goto nextLine;
	}
	// over? last line?
	if ( p == eol ) return true;
	// pretty lines
	//if ( *eol == '\n' ) 
	//	*eol = '\0';
	// need a langid like "en|vs,against"
	char *lang = p;
	p += 2;
	// is it like zh_ch?
	if ( *p == '_' ) p += 3;
	// sanity
	if ( *p != '|' )
		return log("wikt: bad %s file! no lang",filename);
	// null term now
	*p = '\0';
	// skip that
	uint8_t langId = getLangIdFromAbbr ( lang );
	// put char back
	*p = '|';
	// skip the pipe then
	p++;
	// must be there
	if ( langId == 0 ) 
		return log("wikt: bad language abbr in %s",filename);

	//
	// JUST ADD THESE SYNSETS as separate form wiktionary-buf.txt
	// because even if duped it will not matter, Synonyms.cpp dedups
	// all the word forms.
	//

	//
	// since we now only do synonyms at query time and never index them
	// it will make things much easier to deal with when we make mods
	// to this stuff.
	//

	// make it an offset
	int32_t firstLineOffset = lang - m_localBuf.getBufStart();

	// remember first word
	//char *first = p;
	//int64_t baseHash64;

 wordLoop:
	// find end of word
	char *e = p+1;
	for ( ; *e && *e != '\n' && *e != ',' ; e++ );

	// CRAP, hash each word separately???

	// get word hash. ignore spaces in there... we we hash it like
	// a bigram, although if a stopword leads the phrase ids will
	// xor in a special number to prevent "the rapist" from being
	// "therapist". see Phrases.cpp... we do not have trigrams yet
	// so we will have to do like bigram list chaning somehow to
	// simulate trigrams.
	int64_t wh64 = hash64n_nospaces(p,e-p);
	// mangle with language id so Wiktionary::getSynSet() works
	wh64 ^= g_hashtab[0][langId];
	// last of it?
	char *nextWord = NULL;
	if ( *e == ',' ) nextWord = e + 1;
	//
	// now add the words
	//
	// . point to line start... "en|..."
	// . fix "en|read,,centimes,phantasia" for empty word...
	if ( wh64 != 0 &&
	     e-p > 0 &&
	     ! m_localTable.addKey ( &wh64 , &firstLineOffset ) )
		return false;
	// advance to next word
	p = nextWord;
	// add the word into the synset
	if ( p ) goto wordLoop;

	// next line otherwise
	p = eol+1;
	goto nextLine;

	return true;
}

bool Wiktionary::generateHashTableFromWiktionaryTxt ( int32_t sizen ) {

	// for debug
	//sizen = 10000000;
	int32_t round = 0;

	// 
	// FILE FORMAT HELP:
	//
	// https://secure.wikimedia.org/wiktionary/en/wiki/Wiktionary:Entry_layout_explained 
	//  https://secure.wikimedia.org/wiktionary/en/wiki/Wiktionary:Entry_layout_explained/POS_headers
	//
	//
	// i downloaded this file from
	// http://dumps.wikimedia.org/enwiktionary/latest/
	// http://dumps.wikimedia.org/enwiktionary/latest/enwiktionary-latest-abstract.xml
	// THEN i ran split it on like 'split -b 2000000000 wiktionary.txt'
	// to divide it into two files, the first one being 2GB:
	// wiktionary.txt.aa and wiktionary.txt.ab
	// So read those files in here.
	//
	// OUTPUT files:
	//
	// wiktionary-syns.dat  (maps a wordId to ptr into wiktionary-buf.txt)
	// wiktionary-buf.txt   (one syn set per line)
	// wiktionary-lang.txt  (<landId>|<word>\n) (used by Speller.cpp)
	//
	char ff1[256];
	sprintf(ff1, "%swiktionary.txt.aa", g_hostdb.m_dir);
	log(LOG_INFO,"wikt: Loading %s",ff1);
        int fd1 = open ( ff1 , O_RDONLY );
	if ( fd1 < 0 ) {
		log("wikt: open %s : %s",ff1,mstrerror(errno));
		return false;
	}
	// read in whole thing
	int64_t maxReadSize = 300000000; // 300MB
	char *buf = (char *)mmalloc ( maxReadSize + 1 , "wikt" );
	if ( ! buf ) return false;

	int64_t offset = 0LL;

	// use this to scrape popularity info and other words we are missing
	//if ( ! g_speller.init() ) return false;

	// the wiktionary file is like 2.6GB so we can't hold the whole thing
 readInSomeFile:

	// limit to 300MB
	int32_t readSize = sizen;
	if ( readSize > maxReadSize ) readSize = maxReadSize;

	// do not breach file size
	if ( offset + readSize > sizen ) 
		readSize = sizen - offset;

	//
	//
	// ARE WE DONE????
	//
	//
	if ( offset >= sizen ) {
		// don't forget to close
		close ( fd1 );

		// try reading next split file
		if ( round == 0 ) {
			round++;
			offset = 0;
			sprintf(ff1,"%swiktionary.txt.ab",g_hostdb.m_dir);
			log(LOG_INFO,"wikt: Loading %s",ff1);
			int fd1 = open ( ff1 , O_RDONLY );
			if ( fd1 < 0 ) {
				log("wikt: open %s : %s",ff1,mstrerror(errno));
				return false;
			}
			struct stat stats;
			if ( fstat ( fd1 , &stats ) == -1 ) {
				g_errno = errno;
				return false;
			}
			sizen = stats.st_size;
			goto readInSomeFile;
		}

		// do not save if we can't
		if ( g_conf.m_readOnlyMode ) return true;

		// build m_synTable from m_tmp table
		if ( ! compile() ) return false;

		// add unified dict entries into m_langTable if they
		// belong to one and only one language.
		// right now, this just cleans out m_langTable.
		if ( ! integrateUnifiedDict() ) return false;

		log("wikt: testing");
		
		//log("wiktL debug skipping test!");
		test();

		log("wikt: test passed");

		// now save this hash table for quicker loading next time
		//if ( ! m_langTable.save ( g_hostdb.m_dir , 
		//			  "wiktionary-langs.dat" ) )
		//	return false;

		// . and the synomnyms
		// . offsets into m_synBuf, text file of synsets
		if ( ! m_synTable.save ( g_hostdb.m_dir , 
					 "wiktionary-syns.dat" ,
					 NULL,
					 0 ) )
		     //m_synBuf.getBufStart() ,
		     //m_synBuf.length() ) )
			return false;
		// save text file
		if ( m_synBuf.saveToFile ( g_hostdb.m_dir,
					    "wiktionary-buf.txt" ) <= 0 )
			return false;

		if ( m_langBuf.saveToFile(g_hostdb.m_dir,
					    "wiktionary-lang.txt" ) <= 0 )
			return false;


		// this too?
		//if ( ! m_debugMap.save ( g_hostdb.m_dir ,
		//			 "wiktionary-strings.dat",
		//			 m_debugBuf.getBufStart() ,
		//			 m_debugBuf.length() ))
		//    return false;

		// clear this
		m_tmp  .reset();
		m_dedup.reset();

		m_debugMap.reset();
		m_debugBuf.purge();

		m_langBuf.reset();

		return true;
	}

	// log it
	log("wikt: reading %"INT32" bytes of %s @ %"INT64" (filesize=%"INT32")",
	    readSize,ff1,offset,sizen);

	int32_t n = pread ( fd1 , buf , readSize , offset );

	if ( n != readSize ) { 
		log("wikt: read: %s",mstrerror(errno));
		g_errno = EBADENGINEER;
		return false; 
	}

	log("wikt: processing");

	// advance for next read
	offset += n;

	// null terminate
	buf[readSize] = '\0';

	// a hack
	/*
	char *hack = "<title>Bob</title>\n"
		"===Proper noun===\n"
		"{{en-proper noun}}\n"
		"\n"
"# {{given name|male|diminutive=Robert}}.\n"
"# In various fields such as [[cryptography]] and [[physics]], a conventional name for the person or system that receives a message from another person or system conventionally known as [[Alice]].\n"
			  "====Translations====\n"
		"<title>Poo</title>\n"
			  ;
	readSize = gbstrlen(hack);
	gbmemcpy(buf,hack,readSize+1);
	*/

	//
	// simple filter. back to back spaces removed in next loop.
	//
	char *p = buf;
	for ( ; *p ; p++ ) {
		// fix # {{form of|Abbreviation|biography}} for 'bio'
		if ( p[0] == 'f' &&
		     p[1] == 'o' &&
		     p[2] == 'r' &&
		     p[3] == 'm' &&
		     p[4] == ' ' &&
		     p[5] == 'o' &&
		     p[6] == 'f' &&
		     p[7] == '|' &&
		     to_lower_a(p[8]) == 'a' &&
		     to_lower_a(p[9]) == 'b' &&
		     !strncasecmp(p ,"form of|abbreviation|",21) )
			// overwrite the pipe with a space
			gbmemcpy(p    ,"abbreviated  form of|",21);
	}



	char *src = buf;
	char *dst = buf;
	// filter out the annoying bold '''
	for ( ; *src ; src++ ) {
		// skip bold thingy
		if ( src[0] =='\'' &&
		     src[1] =='\'' &&
		     src[2] =='\'' ) {
			src += 2;
			continue;
		}
		// # {{present participle of|''[[snort]]''}}
		if ( src[0] =='\'' &&
		     src[1] =='\'' ) {
			src += 1;
			continue;
		}
		// <space>|  "for |" "form |"
		if ( src[0] == ' ' && 
		     src[1] == '|' )
			continue;
		// filter back-to-back spaces
		if ( src[0] == ' ' && 
		     src[1] == ' ' )
			continue;
		// <space>,
		if ( src[0] == ' ' &&
		     src[1] == ',' )
			continue;
		*dst++ = *src;
	}
	*dst = '\0';


	//
	// . filter the buffer
	// . set "name" to the word we are a form of
	//
	p = buf;
	for ( ; *p ; p++ ) {
		// REWRITE A LINE SEGMENT
		// # {{given name|male|diminutive=Samuel}}
		// # {{given name|male|diminut of|Samuel}}
		if ( p[0] == 'd' &&
		     p[1] == 'i' &&
		     p[2] == 'm' &&
		     !strncmp(p ,"diminutive=",11) ) {
			gbmemcpy(p,"diminut of|",11);
			p += 11;
			continue;
		}

		bool needPound = true;
		// assume no name
		char *name = NULL;
		// REWRITE A FULL LINE
		// # A [[diminutive]] of the male [[given name]] [[Douglas]].\n
		// # {{diminutive form of|Douglas}}                          \n
		if ( p[0] == 'm' &&
		     p[1] == 'a' &&
		     p[2] == 'l' &&
		     !strncmp(p ,"male [[given name]] [[",22) ) {
			needPound = false;
			name = p + 22;
		}
		//# {{given name|female}}, a [[diminutive]] of [[Abigail]].
		if ( p[0] == '[' &&
		     p[1] == '[' &&
		     p[2] == 'd' &&
		     p[3] == 'i' &&
		     !strncmp(p ,"[[diminutive]] of [[",20) ) {
			needPound = false;
			name = p + 20;
		}

		// set needPound = true for this below
		// variant spelling of [[poo]]
		if ( p[0] == 's' &&
		     p[1] == 'p' &&
		     p[2] == 'e' &&
		     p[3] == 'l' &&
		     ! strncasecmp(p ,"spelling of [[",14) )
			name = p + 14;

		// past participle of [[block]]
		if ( p[0] == 'p' &&
		     p[1] == 'a' &&
		     p[2] == 'r' &&
		     p[3] == 't' &&
		     p[4] == 'i' &&
		     ! strncasecmp(p ,"participle of [[",16) ) 
			name = p + 16;


		// past participle of to [[block]]
		if ( p[0] == 'p' &&
		     p[1] == 'a' &&
		     p[2] == 'r' &&
		     p[3] == 't' &&
		     p[4] == 'i' &&
		     ! strncasecmp(p ,"participle of to [[",19) ) 
			name = p + 19;

		// # [[present participle|Present participle]] of [[link]].
		if ( p[0] == 'a' &&
		     p[1] == 'r' &&
		     p[2] == 't' &&
		     p[3] == 'i' &&
		     p[4] == 'c' &&
		     ! strncasecmp(p ,"articiple]] of [[",17) ) 
			name = p + 17;

		// definite [s|S]ingular of [[block]]
		if ( p[0] == 'i' &&
		     p[1] == 'n' &&
		     p[2] == 'g' &&
		     p[3] == 'u' &&
		     p[4] == 'l' &&
		     ! strncasecmp(p ,"ingular of [[",14) ) 
			name = p + 14;

		// # Singular of {{term|airwaves|lang=en}};
		if ( p[0] == 'i' &&
		     p[1] == 'n' &&
		     p[2] == 'g' &&
		     p[3] == 'u' &&
		     p[4] == 'l' &&
		     ! strncasecmp(p ,"ingular of {{term|",18) ) 
			name = p + 18;

		// definite [p|P]lural of [[block]]
		if ( p[0] == 'l' &&
		     p[1] == 'u' &&
		     p[2] == 'r' &&
		     p[3] == 'a' &&
		     p[4] == 'l' &&
		     ! strncasecmp(p ,"lural of [[",11) ) 
			name = p + 11;

		// substitue form for case
		// "objective case of" ... treat like form
		// should fix page for "us" which is "objective case of we"
		bool mangled = false;
		if ( ! name &&
		     p[0] == 'c' &&
		     p[1] == 'a' &&
		     p[2] == 's' &&
		     p[3] == 'e' ) {
			gbmemcpy ( p , "form" , 4 );
			mangled = true;
		}

		// need "form of" for shit below
		if ( ! name &&
		     ( p[0] != 'f' ||
		       p[1] != 'o' ||
		       p[2] != 'r' ||
		       p[3] != 'm' ) )
			continue;

		bool doTailCheck = true;
		if ( name ) doTailCheck = false;

		// # Short form of [[hippopotamus]].
		if ( ! strncasecmp(p-5 ,"past form of",12) )
			name = p + 7;
		if ( ! strncasecmp(p-6 ,"int16_t form of",13) )
			name = p + 7;
		if ( ! strncasecmp(p-6 ,"tense form of",13) )
			name = p + 7;
		if ( ! strncasecmp(p-7 ,"plural form of",14) )
			name = p + 7;
		if ( ! strncasecmp(p-7 ,"dative form of",14) )
			name = p + 7;
		if ( ! strncasecmp(p-8 ,"present form of",15) )
			name = p + 7;
		if ( ! strncasecmp(p-9 ,"familiar form of",16) )
			name = p + 7;
		if ( ! strncasecmp(p-9 ,"singular form of",16) )
			name = p + 7;
		if ( ! strncasecmp(p-9 ,"feminine form of",16) )
			name = p + 7;
		if ( ! strncasecmp(p-9 ,"emphatic form of",16) )
			name = p + 7;
		if ( ! strncasecmp(p-9 ,"genitive form of",16) )
			name = p + 7;
		if ( ! strncasecmp(p-10 ,"int16_tened form of",17) )
			name = p + 7;
		if ( ! strncasecmp(p-10 ,"inflected form of",17) )
			name = p + 7;
		if ( ! strncasecmp(p-10 ,"masculine form of",17) )
			name = p + 7;
		if ( ! strncasecmp(p-10 ,"imperfect form of",17) )
			name = p + 7;
		if ( ! strncasecmp(p-10 ,"objective form of",17) ) 
			name = p + 7;
		if ( ! strncasecmp(p-10 ,"partitive form of",17) ) 
			name = p + 7;
		if ( ! strncasecmp(p-10 ,"reflexive form of",17) ) 
			name = p + 7;
		if ( ! strncasecmp(p-11 ,"diminutive form of",18) )
			name = p + 7;
		if ( ! strncasecmp(p-11 ,"simplified form of",18) )
			name = p + 7;
		if ( ! strncasecmp(p-11 ,"imperative form of",18) )
			name = p + 7;
		if ( ! strncasecmp(p-11 ,"indicative form of",18) )
			name = p + 7;
		if ( ! strncasecmp(p-11 ,"possessive form of",18) ) 
			name = p + 7;
		if ( ! strncasecmp(p-11 ,"accusative form of",18) ) 
			name = p + 7;
		if ( ! strncasecmp(p-12 ,"abbreviated form of",19) )
			name = p + 7;
		if ( ! strncasecmp(p-12 ,"alternative form of",19) ) 
			name = p + 7;
		if ( mangled )
			gbmemcpy ( p , "case" , 4 );
		// skip if no match
		if ( ! name ) continue;

		// then after "of" comes a space
		if ( doTailCheck ) {
			// need to have this
			if ( strncmp(name," [[",3)== 0 ) name += 3;
			// OR YOU CAN HAVE THIS
			// # Past tense and past participle of ''to [[block]]''
			// for title of "blocked". the '' should have been
			// filtered out above.
			else if ( strncmp(name," to [[",6)== 0 ) name += 6;
			// otherwise, forget it!!
			else continue;
		}
		     
		// ok, replace the line with a proper name line
		char *lineStart = p;
		for ( ; lineStart > buf&&*lineStart!='#'&&lineStart[-1]!='\n';
		      lineStart--);
		// need this? this is a numbered line used as a definition 
		// line.
		if ( needPound && *lineStart != '#' ) 
			continue;
		// end end of it
		char *lineEnd = p;
		for ( ; *lineEnd&&*lineEnd !='\n';lineEnd++);
		// temp null that
		char c = *lineEnd;
		*lineEnd = '\0';
		//
		// check for badness
		// i don't like obsolete forms!!! filter out.
		//
		char *bad = NULL;
		if ( ! bad ) bad = gb_strcasestr(lineStart,"archaic");
		if ( ! bad ) bad = gb_strcasestr(lineStart,"rare ");
		if ( ! bad ) bad = gb_strcasestr(lineStart,"less common");
		if ( ! bad ) bad = gb_strcasestr(lineStart,"uncommon ");
		if ( ! bad ) bad = gb_strcasestr(lineStart,"obsolete");
		if ( ! bad ) bad = gb_strcasestr(lineStart,"older ");
		if ( ! bad ) bad = gb_strcasestr(lineStart,"old ");
		if ( ! bad ) bad = gb_strcasestr(lineStart,"nonstandard");
		if ( ! bad ) bad = gb_strcasestr(lineStart,"eye-dialect");
		if ( ! bad ) bad = gb_strcasestr(lineStart,"eye dialect");
		*lineEnd = c;
		if ( bad ) 
			continue;
		// now store a new form
		char *dst = lineStart;
		gbmemcpy(dst,"# {{form|",9);
		dst += 9;
		// point to name
		//char *name = p + 22;
		//
		// PUT it in the proper formation for parsing in the logic 
		// below
		//
		// copy over name
		for ( ; *name !=']' && 
			      *name !='\n' && 
			      *name != '#' &&
			      *name != '|' ; name++ ) 
			*dst++ = *name;
		// close it up
		gbmemcpy(dst,"}}",2);
		dst += 2;
		// panic
		if ( dst > lineEnd ) { char *xx=NULL;*xx=0; }
		// space fill until lineEnd
		for ( ; dst < lineEnd ; dst++ )
			*dst = ' ';
		// skip over that line then
		p = lineEnd;
	}

	// start parsing here
	p = buf;

 wordLoop:

	// look for <title> tag
	char *title = strstr ( p , "<title>" );

	if ( ! title ) goto readInSomeFile;

	// find title after so we know we have a full page
	char *nextTitle = strstr ( title + 5 , "<title" );
	if ( ! nextTitle ) goto readInSomeFile;

	// advance
	p = nextTitle;

	// . scan from title to next title
	// . if it contains "Shavian" then bail! those are stupid
	//   shavian script characters. one of them is int16_t for "of"
	//   so it shows up in of's synset!
	char c;
	if ( nextTitle ) {c = *nextTitle;*nextTitle = '\0';}
	char *found = strstr ( title , "Shavian ");
	if ( nextTitle ) *nextTitle = c;
	if ( found ) goto wordLoop;


	// get the word in the title, <title>
	char *word = title + 7;
	// find end of it
	char *wp = word ; 
	for ( ; *wp && *wp != '<' ; wp++ ) {
		// any space is bad
		if ( is_wspace_a(*wp) ) break;
		// or colon
		if ( *wp == ':' ) break;
		// or * (f*ck)
		if ( *wp == '*' ) break;		
	}
	// bad word that has space or colon in it?
	if ( *wp != '<' ) goto wordLoop;
	// remove any trailing spaces
	for  ( ; wp[-1] == ' ' ; wp-- );
	// if word ends in hyphen skip (anxio-)
	if ( wp[-1] == '-' ) goto wordLoop;
	// or starts with '
	if ( word[0] == '\'' ) goto wordLoop;
	// or ends with ' like "o'" form of "of"
	if ( wp[-1] == '\'' ) goto wordLoop;
	// null term so "title" is null terminated
	*wp = '\0';
	// and skip
	wp++;

	// debug point
	//if ( strcmp(word,"haves")== 0 )
	//	log("hey");

	int32_t flag = 0;
	uint8_t langId = langUnknown;

	bool debug = false;
	//debug = true;

	// set nextline
	char *np = wp;
	for ( ; *np && np < nextTitle ; np++ )
		if ( *np =='#' || (*np == '=' && np[1]=='=') ) break;

 lineLoop:

	// advance to next line. unless its the first line for this word
	// in which np already equals wp. 
	wp = np;

	// . set next line for next call to goto lineLoop.
	// . we do this this way because the code below inserts \0's into
	//   the line for easier parsing...
	np++;
	for ( ; *np == '=' ; np++ );
	for ( ; *np && np < nextTitle ; np++ ) {
		if ( *np =='#' ) break;
		//if ( np[-1] == '\n' ) break;
		if (*np == '=' && np[1]=='=') break;
	}

	// scan for next header OR part of speech description
	//for ( ; *wp && wp < nextTitle ; wp++ )
	//	if ( *wp =='#' || (*wp == '=' && wp[1]=='=') ) break;

	// get next word if no more lines
	if ( ! *wp || wp >= nextTitle ) goto wordLoop;

	// skip line break (\n)
	//if ( *wp == '\n' ) wp++;
	// get next word if no more lines
	//if ( ! *wp || wp >= nextTitle ) goto wordLoop;
	// need a header or a comment here
	//if ( *wp != '=' && *wp != '#' ) goto lineLoop;

	// we got a header, set langid or set POS
	if ( *wp == '=' ) {
		// count em
		int32_t equalCount = 0;
		// skip any extra ='s
		for ( ; *wp == '=' ; wp++ ) equalCount++;
		// if newline follows this equal, it was at the end of
		// an equal pair like "==English=="
		if ( *wp == '\n' )  goto lineLoop;
		// debug
		//int32_t diff = wp - buf;
		//log("diff = %"INT32"",diff);
		// a pos?
		if ( ! strncasecmp(wp,"noun",4) ) {
			flag = WF_NOUN;
			if ( debug ) 
				fprintf(stderr,"%s -> (noun)\n",word);
			addWord ( word, flag , langId , NULL );
			goto lineLoop;
		}
		if ( ! strncasecmp(wp,"verb",4) ) {
			flag = WF_VERB;
			if ( debug ) 
				fprintf(stderr,"%s -> (verb)\n",word);
			addWord ( word, flag , langId , NULL );
			goto lineLoop;
		}
		if ( ! strncasecmp(wp,"participle",10) ) {
			flag = WF_VERB;
			if ( debug ) 
				fprintf(stderr,"%s -> (particple)\n",word);
			addWord ( word, flag , langId , NULL );
			goto lineLoop;
		}
		if ( ! strncasecmp(wp,"preposition",11) ) {
			flag = WF_PREPOSITION;
			if ( debug ) 
				fprintf(stderr,"%s -> (preposition)\n",word);
			addWord ( word, flag , langId , NULL );
			goto lineLoop;
		}
		if ( ! strncasecmp(wp,"interjection",12) ) {
			flag = WF_INTERJECTION;
			if ( debug ) 
				fprintf(stderr,"%s -> (interjection)\n",word);
			addWord ( word, flag , langId , NULL );
			goto lineLoop;
		}
		if ( ! strncasecmp(wp,"pronoun",7) ) {
			flag = WF_PRONOUN;
			if ( debug ) 
				fprintf(stderr,"%s -> (pronoun)\n",word);
			addWord ( word, flag , langId , NULL );
			goto lineLoop;
		}
		if ( ! strncasecmp(wp,"proper",6) ) {
			flag = WF_NOUN; // proper noun
			if ( debug ) 
				fprintf(stderr,"%s -> (proper noun)\n",word);
			addWord ( word, flag , langId , NULL );
			goto lineLoop;
		}
		if ( ! strncasecmp(wp,"abbrev",6) ) {
			flag = WF_ABBREVIATION;//NOUN; // abbreviation
			if ( debug ) 
				fprintf(stderr,"%s -> (abbreviation)\n",word);
			addWord ( word, flag , langId , NULL );
			goto lineLoop;
		}
		if ( ! strncasecmp(wp,"letter",6) ) {
			flag = WF_LETTER;//NOUN; // abbreviation
			if ( debug ) 
				fprintf(stderr,"%s -> (letter)\n",word);
			addWord ( word, flag , langId , NULL );
			goto lineLoop;
		}
		if ( ! strncasecmp(wp,"acronym",6) ) {
			flag = WF_NOUN;
			if ( debug ) 
				fprintf(stderr,"%s -> (acronym)\n",word);
			addWord ( word, flag , langId , NULL );
			goto lineLoop;
		}
		if ( ! strncasecmp(wp,"initialism",10) ) {
			flag = WF_INITIALISM;
			if ( debug ) 
				fprintf(stderr,"%s -> (initialism)\n",word);
			addWord ( word, flag , langId , NULL );
			goto lineLoop;
		}
		if ( ! strncasecmp(wp,"adjective",9) ) {
			flag = WF_ADJECTIVE;
			if ( debug ) 
				fprintf(stderr,"%s -> (adjective)\n",word);
			addWord ( word, flag , langId , NULL );
			goto lineLoop;
		}
		if ( ! strncasecmp(wp,"adverb",6) ) {
			flag = WF_ADVERB;
			if ( debug ) 
				fprintf(stderr,"%s -> (adverb)\n",word);
			addWord ( word, flag , langId , NULL );
			goto lineLoop;
		}
		if ( ! strncasecmp(wp,"article",7) ) {
			flag = WF_ARTICLE;
			if ( debug ) 
				fprintf(stderr,"%s -> (article)\n",word);
			addWord ( word, flag , langId , NULL );
			goto lineLoop;
		}
		// is it a language we support?
		int32_t n = sizeof(s_lowerLangWikiStrings) / sizeof(char *);
		for ( int32_t i = 0 ; i < n ; i++ ) {
			char *str = s_lowerLangWikiStrings[i];
			if ( ! str ) { char *xx=NULL;*xx=0; }
			int32_t  len = strlen(str);
			if ( ! strncasecmp(wp,str,len) ) {
				langId = i;
				if ( debug ) 
					fprintf(stderr,"%s -> (%s)\n",
						word,getLangAbbr(langId));
				addWord ( word, 0 , langId , NULL);
				goto lineLoop;
			}
		}
		// unsupported lang?
		if ( equalCount == 2 ) {
			langId = langUnknown;
			if ( debug ) 
				fprintf(stderr,"%s -> (%s)\n",
					word,getLangAbbr(langId));
			addWord ( word, 0 , langId , NULL );
		}

		// ignore the header otherwise
		goto lineLoop;
	}

	bool gotGoodLine = false;

	// we might have "{{head|tr|abbreviation}} (''[[....
	// which does not start with a #
	//if ( wp[0] == '{' && wp[1] == '{' ) 
	//	gotGoodLine = true;

	// we got a comment
	if ( *wp == '#' ) {
		gotGoodLine = true;
		wp++;
	}

	if ( ! gotGoodLine ) goto lineLoop;

	// save this
	char *lineStart = wp;
	// skip #
	//wp++;
	// skip space
	if ( is_wspace_a(*wp) ) wp++;

	// debug point
	//if ( word[0] == 'b' && word[1] == 'i' && word[2] == 'o' && ! word[3])
	//	log("got bio");

	//
	// SPECIAL case for abbreviations.
	// like for http://en.wiktionary.org/wiki/KS we got
	// # [[Kansas]], a state of the [[United States of America]].
	/*
        if ( flag == WF_ABBREVIATION ||
	     flag == WF_INITIALISM ) {
		// save it
		char *wpsave = wp;
		// forget it if single letter! too much confusion!!
		if ( ! word[1] ) goto skipSpecialLogic;
		// if the line has a '{' in it then do not do this stuff
		// skip until we hit a [[ but stop on # or \n.
		// no! hurts # "{{economics}} [[gross domestic product]]"
		//for ( ; *wp && 
		//	      // if we hit this it might be of proper form
		//	      // like
		//	      // # [[operating system]]; 
		//	      // {{abbreviation of|operativsystem|lang=sv}}
		//	      *wp != '{' &&
		//	      *wp !='#' && 
		//	      *wp !='\n' ; 
		//      wp++ );
		//if ( *wp == '{' ) { wp = wpsave; goto skipSpecialLogic; }
		// restore it
		wp = wpsave;
		// skip until we hit a [[ but stop on # or \n
		for ( ; *wp && 
			      *wp != '[' && 
			      *wp !='#' && 
			      *wp !='\n' ; 
		      wp++ );
		// get [ for abbreviation lists. what are we an abbrev of?
		if ( *wp != '[' ) { wp = wpsave; goto skipSpecialLogic; }
		wp++;
		if ( *wp != '[' ) { wp = wpsave; goto skipSpecialLogic; }
		wp++;
		// skip w: for wikipedia references
		if ( wp[0] == 'w' && wp[1] == ':' ) wp += 2;
		// find ]
		char *wpend = wp + 1;
		for ( ; *wpend && 
			      //[[w:Maltese Cross#United Kingdom|Maltese Cross
			      *wpend != '#' && 
			      //[[w:Maltese Cross#United Kingdom|Maltese Cross
			      *wpend != '|' && 
			      *wpend != ']' ; 
		      wpend++ ) ;
		if ( ! *wpend || *wpend != ']' ) {
			wp = wpsave; goto skipSpecialLogic; }
		// if word ends in '-' toss it out... "centi-" prefix
		if ( wpend[-1] == '-' ) {wp = wpsave; goto skipSpecialLogic; }
		// "w/"
		if ( wpend[-1] == '/' ) {wp = wpsave; goto skipSpecialLogic; }
		*wpend = '\0';
		// get that word then
		//if ( debug ) 
			fprintf(stderr,"%s|%s -> %s"
				"\n"
				//"(%s)\n",
				,getLangAbbr(langId) 
				,word // TITLE!
				,wp
				);
		addWord ( word, flag , langId , wp );
		// try another line
		goto lineLoop;
	}

 skipSpecialLogic:
*/

	// look for something like "{{abbreviation of|Albuquerque|.."
	if ( *wp != '{' ) goto lineLoop;
	wp++;
	if ( *wp != '{' ) goto lineLoop;
	wp++;
	
	// somtimes we got something like
	// # {{education}} {{initialism of|Artium Magister}}
	// so go to next {{'s
	// so skip spaces
	char *secondSet = wp;
	for ( ; *secondSet && *secondSet != '\n'; secondSet++ ) {
		// check
		if ( secondSet[0] == '}' &&
		     secondSet[1] == '}' &&
		     secondSet[2] == ' ' &&
		     secondSet[3] == '{' &&
		     secondSet[4] == '{' ) {
			// skip to the second set of {{}}'s on the 
			// same line
			wp = secondSet += 5;
			break;
		}
	}
	
	// start scan here
	//char *scanStart = wp;
	// assume good
	bool good = false;
	// loop over all little pipe-delineated sections
 scanForFormIndicator:
	// scan until we hit |and not }
	for ( ; *wp && *wp != '}' && *wp != '|' ; wp++ ) {
		// # {{nl-noun-form|pl=1|wijziginkje}}
		if ( wp[0] == 'f' &&
		     wp[1] == 'o' &&
		     wp[2] == 'r' &&
		     wp[3] == 'm' &&
		     wp[4] == '|' )
			good = true;
		// # {{abbeviation of|camarade|...
		if ( wp[0] == ' ' &&
		     wp[1] == 'o' &&
		     wp[2] == 'f' &&
		     wp[3] == '|' )
			good = true;
		// for 'BM' page:
		// # {{head|tr|abbreviation}} (''[[B...
		/*
		if ( wp[0] == 'h' &&
		     wp[1] == 'e' &&
		     wp[2] == 'a' &&
		     wp[3] == 'd' &&
		     wp[4] == '|' )
			good = true;
		*/
	}
	// success?
	if ( *wp != '|' ) goto lineLoop;
	// "of" or "form" must preceed
	if ( ! good ) {
		// maybe try next pipe delineated section
		wp++;
		goto scanForFormIndicator;
	}
	
	
	// broken:
	// # {{conjugation of|livrer||1|s|pres|ind|lang=fr}}
	// # {{form of|third-person singular present|pondre|lang=fr}}
	// # {{plural of|pie|lang=fr}}
	// # {{inflection of|[[pius#Latin|pius]]||voc|m|s|lang=la}}
	// # {{form of|Singular dative masculine|on|lang=cs}}
	
	// skip |
	wp++;
	
	// find terminating '}'
	char *end = wp;
	for ( ; *end && end < nextTitle && *end != '}' ;end++ );
	// try next line if could not find }
	if ( ! *end || end >= nextTitle ) goto lineLoop;
	// null term it
	*end = '\0';
	// in case there was a # in there!
	if ( np < end + 1 ) {
		np = end + 1;
		for ( ; *np && np < nextTitle ; np++ )
			if ( *np =='#' || (*np == '=' && np[1]=='=') )
				break;
	}
	
	
	// nuke all of it! "archaic third person ..."
	if ( gb_strcasestr(lineStart,"archaic ") ) 
		goto lineLoop;
	if ( gb_strcasestr(lineStart,"archaic|") ) 
		goto lineLoop;
	if ( gb_strcasestr(lineStart,"archaic}") ) 
		goto lineLoop;
	// fix 'goest' has {{archaic-verb-form
	if ( gb_strcasestr(lineStart,"{archaic") ) 
		goto lineLoop;
	if ( gb_strcasestr(lineStart,"eye dialect") ) 
		goto lineLoop;
	if ( gb_strcasestr(lineStart,"eye-dialect") ) 
		goto lineLoop;
	// obslete form or spelling
	if ( gb_strcasestr(lineStart,"obsolete ") ) 
		goto lineLoop;
	if ( gb_strcasestr(lineStart,"obsolete|") ) 
		goto lineLoop;
	if ( gb_strcasestr(lineStart,"obsolete}") ) 
		goto lineLoop;
	// {standard of identity|UK} (measurement)
	// prevent cream->UK
	if ( gb_strcasestr(lineStart,"standard ") )
		goto lineLoop;
	// fix 'gwine'
	if ( gb_strcasestr(lineStart,"nonstandard") )
		goto lineLoop;
	
	//
	// now wp = "|.....}" and end = the ending '}'
	//
	// CRAP: # {{sports}} {{initialism of|[[championship|Championship]] [[record|Record]] or [[competition|Competition]] Record}}
	// is messing up on converting pipes to \0 because it
	// ends up mapping "CR" to "championship".
	int32_t inBrackets = 0;
	for ( char *s = wp ; s < end ; s++ ) {
		if ( *s == '[' ) inBrackets++;
		if ( *s == ']' ) inBrackets--;
		if ( *s == '|' && ! inBrackets ) *s = '\0';
	}
	// scan the strings now
	char *start = NULL;
	int32_t slen;
	bool skipNext = false;
	for ( char *s = wp ; s < end ; s += slen + 1 ) {
		slen = strlen(s);
		// skip numbers |1|
		if ( slen == 1 && is_digit(*s) ) continue;
		// skip that {{l|en|... crap {{l|fro|...
		if ( ! strcmp(s,"{{l") ) { skipNext = true; continue;}
		if ( skipNext ) { skipNext = false; continue; }
		// skip certain words
		if ( ! strcmp(s,"pass") ) continue;
		if ( ! strcmp(s,"pres") ) continue;
		if ( ! strcmp(s,"fut") ) continue;
		if ( ! strcmp(s,"nom") ) continue;
		if ( ! strcmp(s,"act") ) continue;
		if ( ! strcmp(s,"voc") ) continue;
		if ( ! strcmp(s,"imp") ) continue;
		if ( ! strcmp(s,"acc") ) continue;
		if ( ! strcmp(s,"ind") ) continue;
		if ( ! strcmp(s,"sub") ) continue;
		if ( ! strcmp(s,"s") ) continue;
		if ( ! strcmp(s,"p") ) continue;
		if ( ! strcmp(s,"m") ) continue;
		if ( ! strcmp(s,"f") ) continue;
		// assignment like "lang=la"
		if ( strstr(s,"=" ) ) continue;
		// third-person singluar
		if ( gb_strcasestr(s,"person ") ) continue;
		if ( gb_strcasestr(s," person") ) continue;
		// third-person
		if ( gb_strcasestr(s,"-person") ) continue;
		// Singular dative masculine
		if ( gb_strcasestr(s,"dative ") ) continue;
		if ( gb_strcasestr(s,"nominative ") ) continue;
		if ( gb_strcasestr(s,"imperative ") ) continue;
		if ( gb_strcasestr(s,"comparative ") ) continue;
		if ( gb_strcasestr(s,"genitive") ) continue;
		if ( gb_strcasestr(s,"possessive ") ) continue;
		if ( gb_strcasestr(s," possessive") ) continue;
		if ( gb_strcasestr(s,"past tense") ) continue;
		// impersonal past
		if ( gb_strcasestr(s," past") ) continue;
		if ( gb_strcasestr(s,"present tense") ) continue;
		if ( gb_strcasestr(s,"future tense") ) continue;
		// passive voice
		if ( gb_strcasestr(s,"passive ") ) continue;
		// present analytic
		if ( gb_strcasestr(s," analytic") ) continue;
		if ( gb_strcasestr(s,"subjunctive ") ) continue;
		if ( gb_strcasestr(s," subjunctive ") ) continue;
		// Postal abbreviation
		if ( gb_strcasestr(s," abbreviation") ) continue;
		// abbreviation of
		if ( gb_strcasestr(s,"abbreviation ") ) continue;
		// infinitive passive
		if ( gb_strcasestr(s,"infinitive ") ) continue;
		// infinitive passive voice
		if ( gb_strcasestr(s," infinitive") ) continue;
		if ( gb_strcasestr(s,"appendix:") ) continue;
		// "form used..."
		if ( gb_strcasestr(s,"form ") ) continue;
		// inflection of
		if ( gb_strcasestr(s,"inflection ") ) continue;
		// front vowel variant
		if ( gb_strcasestr(s," variant") ) continue;
		if ( gb_strcasestr(s," spelling") ) continue;
		if ( gb_strcasestr(s," misspelling") ) continue;
		// definite and plural
		if ( gb_strcasestr(s,"definite") ) continue;
		if ( gb_strcasestr(s,"accusative ") ) continue;
		if ( gb_strcasestr(s,"vocative ") ) continue;
		if ( gb_strcasestr(s,"indicative") ) continue;
		if ( gb_strcasestr(s,"plural") ) continue;
		if ( gb_strcasestr(s,"feminine") ) continue;
		if ( gb_strcasestr(s,"masculine") ) continue;
		if ( gb_strcasestr(s,"oblique") ) continue;
		// singuler definite
		if ( gb_strcasestr(s,"singular ") ) continue;
		if ( gb_strcasestr(s," singular") ) continue;
		// prepositional singluar
		if ( gb_strcasestr(s,"prepositional") ) continue;
		if ( gb_strcasestr(s," participle") ) continue;
		// han form
		if ( gb_strcasestr(s," form") ) continue;
		// *PRENSENT* tense
		if ( gb_strcasestr(s," tense") ) continue;
		if ( gb_strcasestr(s,"lower case") ) continue;
		if ( gb_strcasestr(s,"upper case") ) continue;
		
		// kills the word "present"! so hardcode that!
		if ( ! strcmp(s,"present") ) continue;
		if ( ! strcmp(s,"past") ) continue;
		if ( ! strcmp(s,"capital form") ) continue;
		if ( ! strcmp(s,"capitalized form") ) continue;
		if ( ! strcmp(s,"obsolete capitalization") ) continue;
		if ( ! strcmp(s,"archaic form") ) continue;
		if ( ! strcmp(s,"int16_tened form") ) continue;
		if ( ! strcmp(s,"reduced form") ) continue;
		if ( ! strcmp(s,"unstressed form") ) continue;
		if ( ! strcmp(s,"lowercase form") ) continue;
		if ( ! strcmp(s,"uncapitalized form") ) continue;
		if ( ! strcmp(s,"imperative") ) continue;
		// assume that is it i guess
		start = s;
		break;
	}
	
	// skip if empty!!! wtf??
	if ( ! start ) { wp = end + 1 ; goto lineLoop; }
	// skip ['s and spaces
	// skipping ' made "ve" a form of "of" where it was "'ve"
	for ( ; 
	      *start == '[' || *start == ' ' ; // || *start == '\''; 
	      start++ );
	
	// and ]'s
	char *wend = start + gbstrlen(start);
	for ( ; wend && wend>start && wend[-1] == ']' ;wend--);
	*wend = '\0';
	
	// sometimes they start with w: like for ANZAC:
	// # {{initialism of|[[w:Australian and New Zealand Army Corps|Australian and New Zealand Army Corps]]}}
	if ( start[0]=='w' && start[1]==':' ) {
		start += 2;
		// these are wikipedia titles, skip!
		//goto lineLoop;
	}
	if ( strncasecmp(start,"wikipedia:",10)==0 ) {
		start += 10;
		// these are wikipedia titles, skip!
		//goto lineLoop;
	}
	if ( start[0]==':' && start[1]=='w' && start[2]==':'){
		start += 3;
		// these are wikipedia titles, skip!
		//goto lineLoop;
	}
	
	// nuke after # anchor
	char *a = start;
	for ( ; *a ; a++ ) if ( *a == '#' ) { *a = '\0'; break; }
	// do not add huge words
	if ( gbstrlen(start) > 1000 ) goto lineLoop;
	// skip that
	wp = end + 1;
	
	// or the word " or " in there!
	// identification|Identification]] or [[identity]] [[documentation]
	// # {{comparative of|[[good]] or [[well]]
	a = start;
	for ( ; *a ; a++ ) {
		if ( strncmp(a,"]] or [[",8) == 0 ) {
			*a = '\0';
			break;
		}
	}
	
	// if it has any pipes, i am not dealing with that
	// CRAP: # {{sports}} {{initialism of|[[championship|Championship]] [[record|Record]] or [[competition|Competition]] Record}}
	// cuz it gets too complicated!!!
	a = start;
	int32_t pipeCount = 0;
	for ( ; *a ; a++ ) { if ( *a == '|' ) pipeCount++; }
	a = start;
	// too many pipes?
	if ( pipeCount >= 2 ) 
		goto lineLoop;
	// if just one, pick the first term i guess
	// # {{initialism of|[[w:Americans for Democratic Action|Ame..
	for ( ; *a ; a++ ) { 
		if ( *a == '|' ) {
			// fix
			// {{acronym of|Search for [[extraterrestrial|Extraterrestrial]] Intelligence\0
			char *bs = a;
			for ( ; *bs ; bs++ ) {
				if ( *bs == ']' ) 
					goto lineLoop;
			}
			// ok, good to go
			*a = '\0';
			break;
		}
	}
	
	
	// # {{British|Ireland|dated}} {{initialism of|[[&amp;pound;sd
	// nuke if semicolon
	a = start;
	for ( ; *a ; a++ ) {
		if ( *a == ';' ) goto lineLoop;
		if ( *a == '*' ) goto lineLoop; // f**k
		if ( *a == '+' ) goto lineLoop;
		if ( *a == ',' ) goto lineLoop;
		if ( *a == '{' ) goto lineLoop;
		if ( *a == '}' ) goto lineLoop;
		if ( *a == '(' ) goto lineLoop;
		if ( *a == ')' ) goto lineLoop;
		if ( *a == '/' ) goto lineLoop;
	}
	
	// skip initial spaces again
	for ( ; *start == ' ' ; start++ );
	
	// forget it if ends or begins with hyphen
	if ( start[0]  == '-' ) goto lineLoop;
	if ( a    [-1] == '-' ) goto lineLoop;
	
	// or starts with '
	// fix "'s" for "is"  (the dog's running after me)
	// fix "'ve" as a form of "of"
	if ( start[0] == '\'' ) goto lineLoop;
	
	// same with underscore (fix fotch->_)
	if ( start[0] == '_' ) goto lineLoop;
	if ( a[-1]    == '_' ) goto lineLoop;
	
	// re-write the base word and filter out [ and ]
	char normBuf[1024];
	dst = normBuf;
	src = start;
	for ( ; *src ; src++ ) {
		*dst = *src;
		if ( *dst == '[' ) continue;
		if ( *dst == ']' ) continue;
		dst++;
	}
	*dst = '\0';
	// trim off spaces
	wend = normBuf + gbstrlen(normBuf);
	// fix ''sadden''
	for ( ; wend && wend>normBuf && 
		      (wend[-1] == ']' ||
		       wend[-1] == ' ' ||
		       wend[-1] == '\'' ) ;
	      wend--);
	*wend = '\0';
	
	
	// or starts with '
	// fix "'s" for "is"  (the dog's running after me)
	if ( normBuf[0] == '\'' ) goto lineLoop;
	
	if ( debug ) 
		fprintf(stderr,"%s -> %s"
			"\n"
			//"(%s)\n",
			,word // TITLE!
			,normBuf // baseform! // start
			//getLangAbbr(langId) 
			);
	addWord ( word, flag , langId , normBuf ); // start );
	// try another line
	goto lineLoop;

	// success
	return true;
}

bool Wiktionary::addWord ( char *word , 
			   uint8_t posFlag , 
			   uint8_t langId ,
			   char *formOf ) {

	// done if lang is unknown
	if ( langId == langUnknown ) return true;
	// hash the word
	//int64_t wid = hash64Lower_utf8(word);
	int64_t wid = hash64n(word);

	/*
	// see if already in there
	uint8_t *langIdPtr = (uint8_t *)m_langTableTmp.getValue(&wid);
	// if same
	if ( langIdPtr && *langIdPtr != langId ) {
		// mark it as multi-language, we will delete when done
		*langIdPtr = langTranslingual;
	}
	// otherwise, add it!
	else {
		// . add that then
		// . this only uses 6 byte keys
		if ( ! m_langTableTmp.addKey ( &wid, &langId ) ) return false;
	}
	*/

	// if not form of something make it form of itself
	if ( ! formOf ) formOf = word;

	// to file like dict.cz
	int64_t lk64 = wid ;
	lk64 ^= g_hashtab[4][langId];
	if ( ! m_dedup.isInTable ( &lk64 ) ) {
		m_dedup.addKey ( &lk64 );
		m_langBuf.safePrintf ( "%s|%s\n",
				       getLangAbbr(langId),
				       word);
	}

	// store word so we can map word it to a string
	int32_t len = m_debugBuf.length();
	int32_t wlen = gbstrlen(word);
	if ( ! m_debugMap.isInTable ( &wid ) ) {
		m_debugBuf.safeMemcpy ( word, wlen );
		m_debugBuf.pushChar('\0');
		// this only uess 6 byte keys
		if ( ! m_debugMap.addKey ( &wid , &len ) ) return false;
	}

	// need a POS for adding for synonyms
	//if ( ! posFlag ) return true;
		
	// . get hash of form of
	// . i.e. if word is "jumping" then formOf is "jump"
	// . so this maps "jump" to all the forms it has
	// . thus allowDups is true for this one too
	// . but the "jump" key is language and POS sensitive
	// . so "jump" as a noun does not map to "jumping" (verb) but only
	//   maps to "jumps" the noun
	//int64_t fh64 = hash64Lower_utf8(formOf);
	int64_t fh64 = hash64n(formOf);
	// save that
	int64_t baseForm = fh64;


	// also add formOf
	if ( ! m_debugMap.isInTable ( &baseForm ) ) {
		len = m_debugBuf.length();
		m_debugBuf.safeStrcpy ( formOf );
		m_debugBuf.pushChar('\0');
		// this only uess 6 byte keys
		if ( ! m_debugMap.addKey ( &baseForm , &len ) ) return false;
	}

	//if ( wid == hash64n("fled",0) )
	//	log("hey");
	//if ( formOf != word && fh64 == hash64n("of",0) )
	//	log("hey");
	//if ( (uint64_t)wid == 17808519984823939745ULL ) 
	//	log("stratocumulus");

	// hash in langid
	fh64 ^= g_hashtab[0][langId];
	// include POS flag too i guess
	//fh64 ^= g_hashtab[1][posFlag];

	// dedup table
	int64_t dk64 = hash64h ( fh64 , wid );

	//if ( dk64 == 4174548643612680780LL )
	//	log("boo");

	if ( ! m_dedup.isInTable ( &dk64 ) ) {
		/*
		// the data now includes popularity of wid
		int32_t pop = g_speller.getPhrasePopularity(NULL,
							 wid,
							 true,
							 langId);
		if ( pop > 32000 ) pop = 32000;
		*/
		// make the data
		char data[9];
		gbmemcpy ( data , &wid , 8 );
		data[8] = langId;
		// . add that. allowDups. so you should be able to get all the
		//   forms by just looking at the base form
		// . this uses 8 byte keys
		if ( ! m_tmp.addKey ( &fh64 , data ) ) return false;
		// . add for both
		// . this uses 8 byte keys
		if ( ! m_dedup.addKey ( &dk64 ) ) return false;
	}

	// same for this
	dk64 = hash64h ( fh64 , baseForm );

	//if ( dk64 == 4174548643612680780LL )
	//	log("boo");

	if ( ! m_dedup.isInTable ( &dk64 ) ) {
		/*
		// the data now includes popularity of wid
		int32_t pop = g_speller.getPhrasePopularity(NULL,
							 baseForm,
							 true,
							 langId);
		if ( pop > 32000 ) pop = 32000;
		// make the data
		char data[8];
		gbmemcpy ( data , &baseForm , 6 );
		gbmemcpy ( data + 8 , &pop , 2 );
		*/
		// make the data
		char data[9];
		gbmemcpy ( data , &baseForm , 8 );
		data[8] = langId;
		// . map the base form to itself as well! so compile() works
		//   so if we have the word "jumping" an alt for is "jump"
		// . this uses 8 byte keys
		if ( ! m_tmp.addKey ( &fh64, data ) ) return false;
		// . add for both
		// . this uses 8 byte keys
		if ( ! m_dedup.addKey ( &dk64 ) ) return false;
	}
	
	// success!
	return true;
}

// . make the synonym/form table from m_tmp
// . m_synTable maps a 48-bit wordid (combined with its language id and
//   its part of speeach flag) to a list of alternative forms
//   which are also 48-bit wordids, suitable for hashing into posdb
// . the reason we combine language id and part of speech flag with the
//   word id, is because "jump" the english noun, does not map to
//   "jumping" for example. so we assume a word is a noun only if it
//   could be both a verb or a noun, as in the case of jump or jumps. however,
//   jumping is treated as a verb.
bool Wiktionary::compile ( ) {

	HashTableX dedup;
	dedup.set ( 8,0,16777216,NULL,0,false,0,"cdtab");

	// scan the m_tmp table
	for ( int32_t i = 0 ; i < m_tmp.m_numSlots ; i++ ) {
		// skip empty slots
		if ( ! m_tmp.m_flags[i] ) continue;
		// get this guys key
		int64_t fh64 = m_tmp.getKey64FromSlot(i);
		// is base form "pie"? why doesn't "pie" map to it?
		//if( fh64 == 4935258599006239294LL ) // balon baseform in turk
		//	log("en|UK");
		// do not repeat
		if ( dedup.isInTable ( &fh64 ) ) continue;
		// this uses 8 byte keys
		if ( ! dedup.addKey  ( &fh64 ) ) return false;
		// reset
		//int64_t lastWid = 0LL;
		// remove dups
		HashTableX dd2;
		char dbuf2[512];
		dd2.set(8,0,8,dbuf2,512,false,0,"ddttt2");
		// how many forms? must be 2+ to get added to syntable
		int32_t formCount = 0;
		int32_t stripCount = 0;
		for ( int32_t j = i ; ; j++ ) {
			// wrap around
			if ( j >= m_tmp.m_numSlots ) j = 0;
			// chain stops when we hit empty slot
			if ( ! m_tmp.m_flags[j] ) break;
			// make sure matches
			int64_t kk = m_tmp.getKey64FromSlot(j);
			// must match
			if ( kk != fh64 ) continue;
			// get a form of the base form, wid64
			char *data = (char *)m_tmp.getDataFromSlot(j);

			// must be there
			int32_t *offPtr = (int32_t *)m_debugMap.getValue(data);
			if ( ! offPtr ) { char *xx=NULL;*xx=0; }
			char *word = m_debugBuf.getBufStart() + *offPtr;
			// now re-hash it as lower case
			int64_t wid = hash64Lower_utf8(word);
			// dedup on it
			if ( dd2.isInTable ( &wid ) ) continue;
			dd2.addKey ( &wid );

			// unique
			//if ( *(int64_t *)data == lastWid ) continue;
			// adjacent deduping
			//lastWid = *(int64_t *)data;
			// it matches!
			formCount++;

			// if it has accent marks then we count the stripped
			// version as a form, but we do not have to
			// store the stripped version in wiktionary-buf.txt
			// because it is just a waste of space.
			char a[1024];
			int32_t stripLen = stripAccentMarks(a,
							 1023,
							 (unsigned char *)word,
							 gbstrlen(word));
			if ( stripLen <= 0 ) continue;
			// if same as original word, skip
			int32_t wlen = gbstrlen(word);
			if ( wlen==stripLen && strncmp(a,word,wlen)==0) 
				continue;
			// count as additional form
			stripCount++;
		}
		// need 2+ forms!
		if ( formCount +stripCount <= 1 ) continue;
		// base form
		//int64_t wid = *(int64_t *)m_tmp.getDataFromSlot(i);
		// remember buf start
		int32_t bufLen = m_synBuf.length();
		// remove dups
		HashTableX dd;
		char dbuf[512];
		dd.set(8,0,8,dbuf,512,false,0,"ddttt");
		// a byte for storing the # of synonym forms
		//m_synBuf.pushChar(0);
		// push the langid!
		//m_synBuf.safePrintf("%"INT32",",langId);
		int32_t count = 0;
		// chain for all keys that are the same
		for ( int32_t j = i ; ; j++ ) {
			// wrap around
			if ( j >= m_tmp.m_numSlots ) j = 0;
			// chain stops when we hit empty slot
			if ( ! m_tmp.m_flags[j] ) break;
			// . get key of jth slot
			// . this uses 8 byte keys
			// . kk is the hash of the BASE form i think hashed
			//   with the langid
			int64_t kk = m_tmp.getKey64FromSlot(j);
			// must match
			if ( kk != fh64 ) continue;
			// get a form of the base form, wid64
			char *data = (char *)m_tmp.getDataFromSlot(j);
			// get the word id
			//int64_t wid =*(int64_t *)data;
			// CRAP! this is a case dependent hash! we need 
			// to make it lower case now that the synsets
			// have been established based on case, since 
			// wiktionary is highly case-dependent.
			// get the word itself
			int32_t *offPtr = (int32_t *)m_debugMap.getValue(data);
			// must be there
			if ( ! offPtr ) { char *xx=NULL;*xx=0; }
			char *word = m_debugBuf.getBufStart() + *offPtr;
			// now re-hash it
			int64_t wid = hash64Lower_utf8(word);
			// i bury langid in there
			uint8_t langId = data[8];
			// find "pie"!
			//if ( wid == 1050735555723194583LL )
			//	log("pie");
			// xor in the langid
			wid ^= g_hashtab[0][langId];
			// only add this word form once per langId
			if ( dd.isInTable ( &wid ) ) continue;
			dd.addKey ( &wid );
			// first first time lead with a "<langAbbr>|"
			if ( count == 0 ) {
				m_synBuf.safeStrcpy(getLangAbbr(langId));
				m_synBuf.pushChar('|');
			}
			// first is the wid (6 bytes) then pop (2 bytes)
			// exclude popularity for this
			//m_synBuf.safeMemcpy(data , 6 );
			// print that
			m_synBuf.safeStrcpy(word);
			// comma
			if ( count+1<formCount )
				m_synBuf.pushChar(',');
			// . a ptr to that sequence of alt forms in the buf
			// . this uses 6 byte keys
			m_synTable.addKey(&wid,&bufLen);
			// stratocumulus
			//if ( wid == -1556090671932692078 )
			//	log("stratocumulus");

			//
			// wtf?
			// "won" has two bases "win" and "won"
			// en|won,wons,woned
			// en|win,won,winning,wins
			// and we seem to map to the first one only...
			// so maybe allow dup keys in syntable?
			//

			// . also strip accent marks and add that key as well
			// . so we can map a stripped word to the original
			//   word with accent marks, although it might
			//   actually map to multiple words! so who knows
			//   what to pick, maybe all of them!
			char a[1024];
			int32_t stripLen = stripAccentMarks(a,
							 1023,
							 (unsigned char *)word,
							 gbstrlen(word));
			// debug time
			if ( stripLen > 0 ) a[stripLen] = 0;
			//if ( stripLen > 0 ) 
			//	log("wikt: %"INT32") %s->%s",i,word,a);
			//if ( i==5133265 )
			//	log("hey");
			// if same as original word, ignore it
			if ( stripLen > 0 ) {
				int32_t wlen = gbstrlen(word);
				if ( wlen==stripLen && 
				     strncmp(a,word,wlen) == 0 ) 
					stripLen = 0;
			}
			// if different, add it
			if ( stripLen > 0 ) {
				int64_t swid = hash64Lower_utf8(a);
				// xor in the langid
				swid ^= g_hashtab[0][langId];
				// only add this word form once per langId
				if ( dd.isInTable ( &swid ) ) continue;
				dd.addKey ( &swid );
				// . a ptr to that sequence of alt forms in buf
				// . this uses 6 byte keys
				m_synTable.addKey(&swid,&bufLen);
			}


			// count em up
			count++;
			// limit to 100 synonyms per synset
			if ( count >= 100 ) break;
		}
		// new line
		m_synBuf.pushChar('\n');
		// store the count, the # of syns in this synset
		//char *buf = m_synBuf.getBufStart();
		//buf[bufLen] = (char)count;
		// . and of course the base form. "jump"
		// . no, i add the base form map to itself into m_tmp above
		//   in addWords() now
		//m_synTable.addKey(&baseKey64,&bufLen);
	}

	return true;
}

// add unified dict entries into m_langTable if they
// belong to one and only one language
bool Wiktionary::integrateUnifiedDict ( ) {

	/*
	// scan unified dict
	for ( int32_t i = 0 ; i < numSlots ; i++ ) {
		// skip empty slots
		if ( ! ud->m_flags[i] ) continue;
		// get ptrs
		int32_t off = *(int32_t *)ud->getDataFromSlot(i);
		// refernce
		char *p = g_speller.m_unifiedBuf + off;
		// just one lang?
		if ( ! justOneLang ) continue;
		// skip if already there
		if ( m_langTable.isInTable ( &wid ) ) continue;
		// add it then
		if ( ! m_langTable.addKey ( &wid , &langId ) ) return false;
	}
	*/

	/*
	// scan langtable and remove translingual entries
	for ( int32_t i = 0 ; i < m_langTableTmp.m_numSlots ; i++ ) {
		// skip empty slots
		if ( ! m_langTableTmp.m_flags[i] ) continue;
		// check it
		if ( *(uint8_t *)m_langTableTmp.getDataFromSlot(i) ==
		     langTranslingual ) 
			continue;
		// add it
		char *key = (char *)m_langTableTmp.getKeyFromSlot(i);
		char *val = (char *)m_langTableTmp.getValueFromSlot(i);
		if ( ! m_langTable.addKey ( key , val ) ) return false;
	}
	*/

	return true;
}
