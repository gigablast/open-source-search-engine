#ifdef CYGWIN

#include "Profiler.h"
Profiler::Profiler(){return;}
Profiler::~Profiler(){return;}
bool Profiler::reset(){return true;}
bool Profiler::init(){return true;}
char *Profiler::getFnName(PTRTYPE address,int32_t *nameLen){return NULL;}
void Profiler::stopRealTimeProfiler(const bool keepData){return;}
void Profiler::cleanup(){return;}
bool Profiler:: readSymbolTable(){return true;}
bool sendPageProfiler ( class TcpSocket *s,class HttpRequest *r){return true;}
Profiler g_profiler;	

#else

#include <execinfo.h>
#include <assert.h>
#include "gb-include.h"
#include "Profiler.h"
#include "Stats.h"
#include "sort.h"
#include "Users.h"
Profiler g_profiler;


static int decend_cmpUll ( const void *h1 , const void *h2 );
static int decend_cmpF ( const void *h1 , const void *h2 );
uint32_t *indexTable;
uint32_t *keyTable;
uint64_t *valueTableUll;
float *valueTableF;
//HashTableT<uint32_t, uint64_t> realTimeProfilerData;
#include "HashTableX.h"
HashTableX realTimeProfilerData;
PTRTYPE lastQuickPollAddress = 0;
uint64_t lastQuickPollTime = 0;

Profiler::Profiler() : 
	m_realTimeProfilerRunning(false),
	rtNumEntries(0),
	hitEntries(NULL),
	m_addressMap(NULL),
	m_addressMapSize(0),
	m_rootFrame(0),
	m_lastDeltaAddress(0),
	m_lastDeltaAddressTime(0),
	m_frameTraces(NULL),
	m_numUsedFrameTraces(0)
{
	// SafeBuf newf;
	// newf.safePrintf("%strash/profile.txt",g_hostdb.m_dir);
	// unlink ( newf.getBufStart() );
	//newf.reset();
	// newf.safePrintf("%strash/qp.txt",g_hostdb.m_dir);
	// unlink ( newf.getBufStart() );
}

Profiler::~Profiler() {//reset();
	reset();
}

bool Profiler::reset(){
	m_fn.reset();
	m_lastQPUsed = 0;
	for (int32_t i=0;i<11;i++)
		m_fnTmp[i].reset();
	if(hitEntries)
		mfree(hitEntries, sizeof(HitEntry) * rtNumEntries,
			"hitEntries");
	hitEntries = NULL;
	if(m_addressMap)
		mfree(m_addressMap, sizeof(uint32_t) * m_addressMapSize,
			"m_addressMap");
	m_addressMap = NULL;
	m_activeFns.reset();
	m_quickpolls.reset();

	m_ipBuf.purge();

	return true;
}

bool Profiler::init() {
	m_lastQPUsed = 0;
	// realTimeProfilerData.set(4,8,0,NULL,0,false,0,"rtprof");
        //m_quickpolls.set(4,4,0,NULL,0,false,0,"qckpllcnt");
	// for (int32_t i=0;i<11;i++)
	// 	//m_fnTmp[i].set(256);
	// 	if ( ! m_fnTmp[i].set(4,sizeof(FnInfo),256,NULL,0,false,0,
	// 			      "fntmp"))
	// 		return false;
	// if ( ! m_activeFns.set(4,4,256,NULL,0,false,0,"activefns") )
	// 	return false;
	// if ( ! m_fn.set(4,sizeof(FnInfo),65536,NULL,0,false,0,"fntbl") )
	// 	return false;

	// init Instruction Ptr address count table
	// if ( ! m_ipCountTable.set(8,4,1024*1024,NULL,0,false,0,"proftbl") )
	// 	return false;
	// do not breach
	// if ( ! m_quickpollMissBuf.reserve ( 20000 ) )
	// 	return false;

	if ( m_ipBuf.m_capacity <= 0 &&
	     ! m_ipBuf.reserve ( 5000000 , "profbuf" ) )
		return false;

	return true;
}


// This reads the symbol table of gb executable (thought to be the file gb in 
// the working directory) into a hashtable.
// The gb executable file is in the ELF format, and the code here resembles 
// readelf function in binutils from gnu.org. gb is 32-bits.
bool Profiler:: readSymbolTable(){
	int64_t start=gettimeofdayInMillisecondsLocal();
	struct stat  statbuf;
	//unsigned int i;
	char fileName[512];
	
	sprintf(fileName,"%sgb",g_hostdb.m_dir);
	//Checking to see if the file is present
	if (stat (fileName, & statbuf) < 0)
	{
		log(LOG_INIT,"admin: Cannot stat input file %s."
			 "You must be in the same directory", fileName);
		return false;
	}

	m_file = fopen (fileName, "rb");
	if (m_file == NULL)
	{
		log (LOG_INIT,"admin: Input file %s not found.", fileName);
		return false;
	}
	
	if (! getFileHeader (m_file))
	{
		log(LOG_INIT,"admin: %s, Failed to read file header", fileName);
		fclose (m_file);
		return false;
	}

	processSectionHeaders (m_file);

	/*	process_program_headers (m_file);
	
	process_dynamic_segment (m_file);
	
	process_relocs (m_file);*/
	
	processSymbolTable (m_file);
	
	int64_t end=gettimeofdayInMillisecondsLocal();
	log(LOG_INIT,"admin: Took %"INT64" milliseconds to build symbol table",
		end-start);
	mfree(m_sectionHeaders,m_elfHeader.e_shnum * sizeof (Elf_Internal_Shdr),
		"ProfilerD");
	mfree(m_stringTable,m_stringTableSize,"ProfilerB");
	fclose (m_file);
	return true;
}


bool Profiler:: getFileHeader (FILE * file){
	/* Read in the identity array.  */
	if (fread (m_elfHeader.e_ident, EI_NIDENT, 1, file) != 1)
		return 0;
	
	/* Determine how to read the rest of the header.  */
	//Found that gb is little_endian
	//Found that bfd_vma type is uint32_t	
	//gb is supposed to be 32-bit
	/* Read in the rest of the header.  */
	Elf32_External_Ehdr ehdr32;
	
	if (fread (ehdr32.e_type, sizeof (ehdr32) - EI_NIDENT, 1, file) != 1)
		return 0;
	
	m_elfHeader.e_type      = getByte (ehdr32.e_type,sizeof(ehdr32.e_type));
	m_elfHeader.e_machine   = getByte (ehdr32.e_machine,sizeof(ehdr32.e_machine));
	m_elfHeader.e_version   = getByte (ehdr32.e_version,sizeof(ehdr32.e_version));
	m_elfHeader.e_entry     = getByte (ehdr32.e_entry,sizeof(ehdr32.e_entry));
	m_elfHeader.e_phoff     = getByte (ehdr32.e_phoff,sizeof(ehdr32.e_phoff));
	m_elfHeader.e_shoff     = getByte (ehdr32.e_shoff,sizeof(ehdr32.e_shoff));
	m_elfHeader.e_flags     = getByte (ehdr32.e_flags,sizeof(ehdr32.e_flags));
	m_elfHeader.e_ehsize    = getByte (ehdr32.e_ehsize,sizeof(ehdr32.e_ehsize));
	m_elfHeader.e_phentsize = getByte (ehdr32.e_phentsize,sizeof(ehdr32.e_phentsize));
	m_elfHeader.e_phnum     = getByte (ehdr32.e_phnum,sizeof(ehdr32.e_phnum));
	m_elfHeader.e_shentsize = getByte (ehdr32.e_shentsize,sizeof(ehdr32.e_shentsize));
	m_elfHeader.e_shnum     = getByte (ehdr32.e_shnum,sizeof(ehdr32.e_shnum));
	m_elfHeader.e_shstrndx  = getByte (ehdr32.e_shstrndx,sizeof(ehdr32.e_shstrndx));
	return 1;
}

uint32_t Profiler::getByte (unsigned char * field,int size){
	switch (size)
	{
	case 1:
		return * field;
		
	case 2:
		return  ((unsigned int) (field [0]))
	|    (((unsigned int) (field [1])) << 8);
		
	case 8:
		/* We want to extract data from an 8 byte wide field and
		   place it into a 4 byte wide field.  Since this is a little
		   endian source we can juts use the 4 byte extraction code.  */
		/* Fall through.  */
	case 4:
		return  ((uint32_t) (field [0]))
			|    (((uint32_t) (field [1])) << 8)
			|    (((uint32_t) (field [2])) << 16)
			|    (((uint32_t) (field [3])) << 24);
	default:
		log(LOG_INIT,"admin: Unhandled data length: %d", size);
		char *xx=NULL; xx=0;
		return 0;
	}
	return 0;
}


static int s_addressMapCmp(const void *A, const void *B) {
	if(*(uint32_t*)A < *(uint32_t*)B) return -1;
	else if(*(uint32_t*)A > *(uint32_t*)B) return 1;
	return 0;
}

bool Profiler::processSymbolTable (FILE * file){
	Elf_Internal_Shdr *   section;
	unsigned int i;
	Elf_Internal_Shdr * string_sec = NULL;

	if(m_addressMap)
		mfree(m_addressMap, sizeof(uint32_t) * m_addressMapSize, "m_addressMap");

	m_addressMapSize = 0;

	for(i = 0, section = m_sectionHeaders; i < m_elfHeader.e_shnum; ++i, ++section) {
		if (   section->sh_type != SHT_SYMTAB
		       && section->sh_type != SHT_DYNSYM)
			continue;
		unsigned int si;
		Elf_Internal_Sym *    symtab;
		Elf_Internal_Sym *    psym;
		symtab = get32bitElfSymbols(file, section->sh_offset,
					    section->sh_size / section->sh_entsize);
		if(!symtab) continue;
		for (si = 0, psym = symtab;
		     si < section->sh_size / section->sh_entsize;
		     si ++, psym ++)
			if (((int32_t)psym->st_size)>0)
				++m_addressMapSize;
		mfree (symtab,
		       (section->sh_size/section->sh_entsize)*sizeof(Elf32_External_Sym),
		       "ProfilerF");
	}

	m_addressMap = (uint32_t *)mmalloc(sizeof(uint32_t) * m_addressMapSize, "m_addressMap");
	m_lastAddressMapIndex = 0;

	for (i = 0, section = m_sectionHeaders;
	     i < m_elfHeader.e_shnum;
	     i++, section++){
		unsigned int          si;
		char *                strtab;
		Elf_Internal_Sym *    symtab;
		Elf_Internal_Sym *    psym;
	  
		if (   section->sh_type != SHT_SYMTAB
		       && section->sh_type != SHT_DYNSYM)
			continue;
	  
		log(LOG_INIT,"admin: Symbol table '%s' contains %"UINT32" entries",
		    m_stringTable+section->sh_name,
		    (uint32_t) (section->sh_size / section->sh_entsize));
		//log(LOG_WARN,"Profiler:   Num\t   Value\t  Size\t    Name");
		symtab = get32bitElfSymbols(file, section->sh_offset,
					    section->sh_size / section->sh_entsize);
		if (symtab == NULL)
			continue;
	  
		if (section->sh_link == m_elfHeader.e_shstrndx)
			strtab = m_stringTable;
		else{
			string_sec = m_sectionHeaders + section->sh_link;

			if (fseek (m_file,string_sec->sh_offset, SEEK_SET)){
				log(LOG_INIT,"admin: Unable to seek to start "
				    "of %s at %"XINT32"", "string table",
				    string_sec->sh_offset);
				return 0;
			}
			strtab = (char *) mmalloc (string_sec->sh_size,
						   "ProfilerG");
			if (strtab == NULL){
				log(LOG_INIT,"admin: Out of memory allocating "
				    "%"INT32" bytes for %s", string_sec->sh_size,
				    "string table");
			}
			if (fread ( strtab, string_sec->sh_size, 1, 
				    m_file) != 1 ){
				log(LOG_INIT,"admin: Unable to read in %"INT32" "
				    "bytes of %s", string_sec->sh_size,
				    "string table");
				mfree (strtab,string_sec->sh_size,"ProfilerG");
				strtab = NULL;
				return 0;
			}
		}
		for (si = 0, psym = symtab;
		     si < section->sh_size / section->sh_entsize;
		     si ++, psym ++){
			if (((int32_t)psym->st_size)>0){
				//				FnInfo *fnInfo;
				int32_t key = psym->st_value;
				int32_t slot=m_fn.getSlot(&key);
				if (slot!=-1){
					//fnInfo=m_fn.getValuePointerFromSlot(slot);
					//This is happeninig because the 
					// symbol table stores the same 
					// variable through two section headers
					//There is a WEAK type and a GLOBAL 
					// type. Doesn't seem to create a
					// problem for the profiler
					// log(LOG_WARN,"Profiler: Two "
					// "functions pointing to "
					// "same address space %"INT32"",
					// (int32_t)psym->st_value);
				}
				else{
					FnInfo fnInfoTmp;
					strncpy(fnInfoTmp.m_fnName,
						strtab+psym->st_name,255);
					
					char* end = strnstr2(fnInfoTmp.
							     m_fnName,
							    255, "__");
					if(end) 
						*end = '\0';
					else 
						fnInfoTmp.m_fnName[gbstrlen(strtab+psym->st_name)]='\0';
					fnInfoTmp.m_timesCalled=0;
					fnInfoTmp.m_totalTimeTaken=0;
					fnInfoTmp.m_maxTimeTaken=0;	
					fnInfoTmp.m_numCalledFromThread=0;
					fnInfoTmp.m_inFunction = 0;
					fnInfoTmp.m_maxBlockedTime = 0;
					fnInfoTmp.m_lastQpoll = "";
					fnInfoTmp.m_prevQpoll = "";
					uint32_t address=(int32_t)psym->st_value;
					//log(LOG_WARN,"Profiler: Adding fninfo name=%s, key=%"INT32"",
					// fnInfo->m_fnName,address);
					int32_t key = (int32_t)address;
					m_fn.addKey(&key,&fnInfoTmp);
					m_addressMap[m_lastAddressMapIndex++] = address;
				}
			}
			/*log(LOG_WARN,"%6d\t %8.8lx\t   %5ld\t   %s", 
			    si,(uint32_t)psym->st_value,
			    (int32_t)psym->st_size,strtab + psym->st_name);*/
		}
		mfree (symtab,
		       (section->sh_size/section->sh_entsize)*sizeof(Elf32_External_Sym),
		       "ProfilerF");
		if (strtab != m_stringTable)
			mfree (strtab,string_sec->sh_size,"ProfilerG");
	}
	gbqsort(m_addressMap, m_lastAddressMapIndex, sizeof(uint32_t), s_addressMapCmp);
      	return 1;
}

Elf_Internal_Sym *Profiler::get32bitElfSymbols(FILE * file,
					       uint32_t offset,
					       uint32_t number){	
	Elf32_External_Sym* esyms;
	Elf_Internal_Sym *isyms;
	Elf_Internal_Sym *psym;
	unsigned int j;
	
	//	GET_DATA_ALLOC (offset, number * sizeof (Elf32_External_Sym),
	//  esyms, Elf32_External_Sym *, "symbols");
	
	if (fseek(file, offset, SEEK_SET)){
		log(LOG_INIT,"admin: Unable to seek to start of %s at %"XINT32"", "symbols", offset);
		return 0;
	}
	esyms = (Elf32_External_Sym *) 
		mmalloc (number * sizeof (Elf32_External_Sym),"ProfilerE");
	if (esyms==NULL){
		log(LOG_INIT,"admin: Out of memory allocating %"INT32" bytes for %s",
		    number *(int32_t)sizeof (Elf32_External_Sym),"Symbols");
		return 0;
	}

	if (fread (esyms,number * sizeof (Elf32_External_Sym), 1, file) != 1){ 
		log(LOG_INIT,"admin: Unable to read in %"INT32" bytes of %s", 
		    number * (int32_t)sizeof (Elf32_External_Sym), "symbols");
		mfree (esyms,number * sizeof (Elf32_External_Sym),"ProfilerE");
		esyms = NULL;
		return 0;
	}
	int32_t need = number * sizeof (Elf_Internal_Sym);
	isyms = (Elf_Internal_Sym *) mmalloc (need,"ProfilerF");
	
	if (isyms == NULL){
		log(LOG_INIT,"admin: Out of memory");
		mfree (esyms,number * sizeof (Elf32_External_Sym),"ProfilerF");		
		return NULL;
	}
	
	for (j = 0, psym = isyms;
	     j < number;
	     j ++, psym ++){
		psym->st_name  = getByte (esyms[j].st_name,sizeof(esyms[j].st_name));
		
		psym->st_value = getByte (esyms[j].st_value,sizeof(esyms[j].st_value));
		psym->st_size  = getByte (esyms[j].st_size,sizeof(esyms[j].st_size));
		psym->st_shndx = getByte (esyms[j].st_shndx,sizeof(esyms[j].st_shndx));
		psym->st_info  = getByte (esyms[j].st_info,sizeof(esyms[j].st_info));
		psym->st_other = getByte (esyms[j].st_other,sizeof(esyms[j].st_other));
	}
	
	mfree (esyms,number * sizeof (Elf32_External_Sym),"ProfilerE");
	return isyms;
}




bool Profiler::processSectionHeaders (FILE * file){
	Elf_Internal_Shdr * section;
	
	m_sectionHeaders = NULL;
	
	if (m_elfHeader.e_shnum == 0)
	{
		return 1;
	}
	if (! get32bitSectionHeaders (file))
		return 0;

	/* Read in the string table, so that we have names to display.  */
	section = m_sectionHeaders + m_elfHeader.e_shstrndx;
	
	if (section->sh_size != 0)
	{
		if (fseek (m_file,section->sh_offset,SEEK_SET)){
			log(LOG_INIT,"admin: Unable to seek to start of %s "
			    "at %"XINT32"\n","string table",section->sh_offset);
			return 0;
		}
		m_stringTableSize=section->sh_size;
		m_stringTable = (char *) mmalloc (m_stringTableSize,
						  "ProfilerB");
		if (m_stringTable == NULL){
			log(LOG_INIT,"admin: Out of memory allocating %"INT32" "
			    "bytes for %s\n", section->sh_size,"string table");
			return 0;
		}
		if (fread (m_stringTable, section->sh_size, 1, m_file) != 1){
			log(LOG_INIT,"admin: Unable to read in %"INT32" bytes of "
			    "%s\n",section->sh_size,"section table");
			mfree (m_stringTable,m_stringTableSize,"ProfilerB");
			m_stringTable = NULL;
			return 0;
		}
	}	
	return 1;
}

	
bool Profiler::get32bitSectionHeaders (FILE * file){
	Elf32_External_Shdr * shdrs;
	Elf_Internal_Shdr * internal;
	unsigned int          i;

	if (fseek (m_file, m_elfHeader.e_shoff, SEEK_SET)){
		log(LOG_INIT,"admin: Unable to seek to start of %s at %"XINT32"\n",
		    "section headers", m_elfHeader.e_shoff);
		return 0;
	}
	
	shdrs = (Elf32_External_Shdr *) mmalloc 
		(m_elfHeader.e_shentsize * m_elfHeader.e_shnum,"ProfilerC");
	if (shdrs == NULL){
		log(LOG_INIT,"admin: Out of memory allocating %d bytes for "
		    "%s\n", m_elfHeader.e_shentsize * m_elfHeader.e_shnum,
		    "section headers");
		return 0;
	}
	
	if (fread (shdrs,m_elfHeader.e_shentsize * m_elfHeader.e_shnum, 
		   1,m_file) != 1){
		log(LOG_INIT,"admin: Unable to read in %d bytes of %s\n", 
		    m_elfHeader.e_shentsize * m_elfHeader.e_shnum,
		    "section headers");
		mfree (shdrs,m_elfHeader.e_shentsize * m_elfHeader.e_shnum,
		       "ProfilerC");
		shdrs = NULL;
		return 0;
	}

	m_sectionHeaders = (Elf_Internal_Shdr *) mmalloc
		(m_elfHeader.e_shnum * sizeof (Elf_Internal_Shdr),"ProfilerD");
	
	if (m_sectionHeaders == NULL){
		log(LOG_INIT,"admin: Out of memory\n");
		return 0;
	}
	
	for (i = 0, internal = m_sectionHeaders;
	     i < m_elfHeader.e_shnum;
	     i ++, internal ++){
		internal->sh_name = getByte (shdrs[i].sh_name,sizeof(shdrs[i].sh_name));
		internal->sh_type = getByte (shdrs[i].sh_type,sizeof(shdrs[i].sh_type));
		internal->sh_flags= getByte (shdrs[i].sh_flags,sizeof(shdrs[i].sh_flags));
		internal->sh_addr = getByte (shdrs[i].sh_addr,sizeof(shdrs[i].sh_addr));
		internal->sh_offset= getByte (shdrs[i].sh_offset,sizeof(shdrs[i].sh_offset));
		internal->sh_size = getByte (shdrs[i].sh_size,sizeof(shdrs[i].sh_size));
		internal->sh_link = getByte (shdrs[i].sh_link,sizeof(shdrs[i].sh_link));
		internal->sh_info = getByte (shdrs[i].sh_info,sizeof(shdrs[i].sh_info));
		internal->sh_addralign= getByte (shdrs[i].sh_addralign,sizeof(shdrs[i].sh_addralign));
		internal->sh_entsize = getByte (shdrs[i].sh_entsize,sizeof(shdrs[i].sh_entsize));
	}
	mfree (shdrs,m_elfHeader.e_shentsize * m_elfHeader.e_shnum,
	       "ProfilerC");
	return 1;
}


bool Profiler::startTimer(int32_t address, const char* caller) {
	// disable - we do interrupt based profiling now
	return true;
	if(g_inSigHandler) return 1;
	int32_t slot = m_fn.getSlot(&address);
	FnInfo *fnInfo;
	if (slot == -1) return false;
	fnInfo=(FnInfo *)m_fn.getValueFromSlot(slot);

	if(fnInfo->m_inFunction++ > 0) return true;

	fnInfo->m_startTimeLocal = gettimeofdayInMillisecondsLocal();
	fnInfo->m_lastPauseTime = fnInfo->m_startTimeLocal;
	fnInfo->m_startTime = gettimeofdayInMilliseconds();
	m_activeFns.addKey(&address, &fnInfo);
	return true;
}

inline uint64_t gettimeofdayInMicroseconds(void) {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return(((uint64_t)tv.tv_sec * 1000000LL) + (uint64_t)tv.tv_usec);
}
bool Profiler::pause(const char* caller, int32_t lineno, int32_t took) {
	lastQuickPollTime = gettimeofdayInMicroseconds(); 
	uint64_t nowLocal = lastQuickPollTime / 1000;
	void *trace[3];
	backtrace(trace, 3);
	const void *stackPtr = trace[2];
	lastQuickPollAddress = (PTRTYPE)stackPtr; 
	for(int32_t i = 0; i < m_activeFns.getNumSlots(); i++) {
		//if(m_activeFns.getKey(i) == 0) continue;
		if ( m_activeFns.isEmpty(i) ) continue;
		FnInfo* fnInfo = *(FnInfo **)m_activeFns.getValueFromSlot(i);
		uint64_t blockedTime = nowLocal - 
			fnInfo->m_lastPauseTime ;
		if (blockedTime > fnInfo->m_maxBlockedTime) {
			fnInfo->m_maxBlockedTime = blockedTime;
			fnInfo->m_lastQpoll = caller;
			fnInfo->m_prevQpoll = m_lastQpoll;
		}
		fnInfo->m_lastPauseTime = nowLocal;
	}
	
	if(!caller || took < 0) return true;


	//break here in gdb and go up on the stack if you want to find a place
	//to add a quickpoll!!!!1!!
//   	if(took > 50)
// 	   log(LOG_WARN, "admin qp %s--%"INT32" took %"INT32"",
// 	       caller, lineno, took);
	PTRTYPE qpkey = (PTRTYPE)caller + lineno;
	int32_t slot = m_quickpolls.getSlot(&qpkey);
	if(slot < 0) {
		if(m_lastQPUsed >= 512) {
			log(LOG_WARN, "admin: profiler refusing to add to "
			    "full quickpoll table.");
			return true;
		}

		QuickPollInfo* q = &m_quickPollInfos[m_lastQPUsed++];
		memset(q, 0, sizeof(QuickPollInfo));
		q->m_caller     = caller;
		q->m_lineno     = lineno;
		q->m_last       = m_lastQpoll;
		q->m_lastlineno = m_lastQpollLine;
		q->m_timeAcc    = took;
		q->m_maxTime    = took;
		q->m_times      = 1;
		m_quickpolls.addKey(&qpkey, &q);
	}
	else {
		QuickPollInfo* q = *(QuickPollInfo **)m_quickpolls.getValueFromSlot(slot);
		if(q->m_maxTime < took) {
			q->m_caller     = caller;
			q->m_lineno     = lineno;
			q->m_last       = m_lastQpoll;
			q->m_lastlineno = m_lastQpollLine;
			q->m_maxTime = took;
		}
		q->m_timeAcc += took;
		q->m_times++;
	}

	m_lastQpoll = caller;
	m_lastQpollLine = lineno;

	return true;
}

bool Profiler::unpause() {
	uint64_t nowLocal = gettimeofdayInMillisecondsLocal();
 	for(int32_t i = 0; i < m_activeFns.getNumSlots(); i++) {
		//if(m_activeFns.getKey(i) == 0) continue;
		if ( m_activeFns.isEmpty(i) ) continue;
		FnInfo* fnInfo = *(FnInfo **)m_activeFns.getValueFromSlot(i);
		fnInfo->m_lastPauseTime = nowLocal;
 	}
	return true;
}

bool Profiler::endTimer(int32_t address,
			const char *caller,
			bool isThread ) {
	// disable - we do interrupt based profiling now
	if(g_inSigHandler) return 1;
	FnInfo *fnInfo;
	int32_t slot = m_activeFns.getSlot(&address);
	if (slot < 0 ) {
		//log(LOG_WARN,"Profiler: got a non added function at 
		// address %"INT32"",address);
		// This happens because at closing the profiler is still on
		// after destructor has been called. Not displaying address
		// because is is of no use
		//		{ char *xx = NULL; *xx = 0; }
		//		return false;
		return true;
	}
	fnInfo=*(FnInfo **)m_activeFns.getValueFromSlot(slot);
	if(--fnInfo->m_inFunction > 0) return true;

	uint64_t nowLocal = gettimeofdayInMillisecondsLocal();
	//uint64_t now = gettimeofdayInMilliseconds();
	uint64_t timeTaken = nowLocal - fnInfo->m_startTimeLocal;

	uint64_t blockedTime = nowLocal - fnInfo->m_lastPauseTime ;
	if (blockedTime > fnInfo->m_maxBlockedTime) {
		fnInfo->m_maxBlockedTime = blockedTime;
		fnInfo->m_prevQpoll = fnInfo->m_lastQpoll;
		fnInfo->m_lastQpoll = caller;
	}
	
	fnInfo->m_totalTimeTaken += timeTaken;
	fnInfo->m_timesCalled++;
	if (timeTaken > (fnInfo->m_maxTimeTaken))
		fnInfo->m_maxTimeTaken = timeTaken;
	if (isThread)
		fnInfo->m_numCalledFromThread++;

	m_activeFns.removeKey(&address);
	char* name = getFnName(address);


	if (timeTaken > (uint32_t)g_conf.m_minProfThreshold) {
		if(g_conf.m_sequentialProfiling)
			log(LOG_TIMING, "admin: %"INT64" ms in %s from %s", 
			    timeTaken, 
			    name,
			    caller?caller:"");

		/*
		if(g_conf.m_dynamicPerfGraph) {
			g_stats.addStat_r ( 0      , 
					    fnInfo->m_startTime, 
					    now,
					    "profiler_stat",
					    0 , //color will be the hash of 
					    //the callback
					    STAT_GENERIC,
					    name);		
		}
		*/
	}

	for (int32_t i=0;i<11;i++){
		//if we find a hashtable is less than 1 second old
		uint64_t diffTime=nowLocal-m_fnTime[i];
		if((diffTime<1000)&&(m_fnTime[i]!=0)){
			//Add this function. Don't add the function name,
			//shall get that from m_fn
			//log(LOG_WARN,"Profiler: adding funtion to existing "
			//"hashtable i=%"INT32",now=%"INT64","
			// "m_fnTime=%"INT64", diffTime=%"INT64"",i,now,
			// m_fnTime[i],diffTime);
			slot=m_fnTmp[i].getSlot(&address);
			if (slot!=-1){
				fnInfo=(FnInfo *)m_fnTmp[i].
					getValueFromSlot(slot);
				fnInfo->m_totalTimeTaken+=timeTaken;
				fnInfo->m_timesCalled++;
				if (timeTaken>(fnInfo->m_maxTimeTaken))
					fnInfo->m_maxTimeTaken=timeTaken;
				if(isThread)
					fnInfo->m_numCalledFromThread++;
			}
			else {
				FnInfo fnInfoTmp;
				fnInfoTmp.m_timesCalled=1;
				fnInfoTmp.m_totalTimeTaken=timeTaken;
				fnInfoTmp.m_maxTimeTaken=timeTaken;
				if (isThread)
					fnInfoTmp.m_numCalledFromThread=1;
				else
					fnInfoTmp.m_numCalledFromThread=0;
				m_fnTmp[i].addKey(&address,&fnInfoTmp);
			}
			return true;
		}
	}
	//if not, then find a hashtable that is more than 10 seconds old
	//and replace it with the new hashtable
	for (int32_t i=0;i<11;i++){
		uint64_t diffTime=nowLocal-m_fnTime[i];
		if((diffTime>=10000) || (m_fnTime[i]==0)){
			/*log(LOG_WARN,"Profiler: m_fntime=%"INT64",i=%"INT32",now=%"INT64",diffTime=%"INT64"",
			  m_fnTime[i],i,now,diffTime);*/
			//First clear the hashtable
			m_fnTmp[i].clear();						
			//Add this function
			FnInfo fnInfoTmp;
			fnInfoTmp.m_timesCalled=1;
			fnInfoTmp.m_totalTimeTaken=timeTaken;

			fnInfoTmp.m_maxTimeTaken=timeTaken;
			if (isThread)
				fnInfoTmp.m_numCalledFromThread=1;
			else
				fnInfoTmp.m_numCalledFromThread=0;
			m_fnTmp[i].addKey(&address,&fnInfoTmp);

			//Change time of hashtable
			m_fnTime[i]=nowLocal;
			return true;
		}
	}
	return true;
}

bool Profiler::printInfo(SafeBuf *sb,char *username, //int32_t user, 
                         char *pwd, char *coll, 
			 int sorts,int sort10, int qpreset,
			 int profilerreset) {
	// sort by max blocked time by default
	if ( sorts == 0 ) sorts = 8;

	int32_t slot;
	uint32_t key(0);
	int32_t numSlots = m_fn.getNumSlots();
	int32_t numSlotsUsed = m_fn.getNumSlotsUsed();
	FnInfo *fnInfo;

	if ( profilerreset ) {
		for ( int32_t i = 0; i < m_fn.getNumSlots(); i++ ){
			//key=m_fn.getKey(i);
			//if (key!=0){
			if ( ! m_fn.isEmpty(i) ) {
				fnInfo=(FnInfo *)m_fn.getValueFromSlot(i);
				// set everything to 0
					fnInfo->m_timesCalled = 0;
					fnInfo->m_totalTimeTaken = 0;
					fnInfo->m_maxTimeTaken = 0;
					fnInfo->m_numCalledFromThread = 0;
					fnInfo->m_maxBlockedTime = 0;
			}		
		}
	}


	sb->safePrintf(  "<center>\n<table %s>\n"
			 "<tr class=hdrow><td colspan=9>"
			 "<center><b>Profiler "//- Since Startup</b></center>"
			 "<a href=\"/admin/profiler?c=%s"//"
			 "&profilerreset=1\">"
			 "(reset)</a></b></center>"
			 "</td></tr>\n",
			 TABLE_STYLE,
			 coll);

       	sb->safePrintf("<tr bgcolor=#%s>"
		       "<td><b>Address</b></td><td><b>Function</b></td>"
		       , LIGHT_BLUE);
	sb->safePrintf("<td><b><a href=/admin/profiler?sorts=3&c=%s>"
		       "Times Called</a></b></td></td>",coll);
	sb->safePrintf("<td><b><a href=/admin/profiler?sorts=4&c=%s>"
		       "Total Time(msec)</a></b></td></td>",coll);
	sb->safePrintf("<td><b><a href=/admin/profiler?sorts=5&c=%s>"
		       "Avg Time(msec)</b></a></td>",coll);
	sb->safePrintf("<td><b><a href=/admin/profiler?sorts=6&c=%s>"
		       "Max Time(msec)</a></b></td>",coll);
	sb->safePrintf("<td><b><a href=/admin/profiler?sorts=7&c=%s>"
		       "Times from Thread</a></b></td>",coll);
	sb->safePrintf("<td><b><a href=/admin/profiler?sorts=8&c=%s>"
		       "Max Blocked Time</a></b></td>",coll);
// 	sb->safePrintf("<td><b><a href=/admin/profiler?sorts=8&c=%s>"
// 		       "Between Quick Polls</a></b></td></tr>",coll);

	indexTable=(uint32_t*) 
		mcalloc(numSlotsUsed*sizeof(uint32_t),"ProfilerW");
	keyTable=(uint32_t*) mcalloc
		(numSlotsUsed*sizeof(uint32_t),"ProfilerX");
	if(sorts==5 ||sort10==5)
		valueTableF=(float*) 
			mcalloc(numSlotsUsed*sizeof(float),"ProfilerY");
	else
		valueTableUll=(uint64_t*) 
			mcalloc(numSlotsUsed*sizeof(uint64_t),
				"ProfilerY");
	int32_t numFnsCalled=0;
	for (int32_t i=0;i<numSlots;i++){
		//key=m_fn.getKey(i);
		//if (key!=0){
		if ( !m_fn.isEmpty(i) ) {
			fnInfo=(FnInfo *)m_fn.getValueFromSlot(i);
			// To save calculating time, just store functions
			// that have been called
			if(fnInfo->m_timesCalled!=0){
				keyTable[numFnsCalled]=key;
				indexTable[numFnsCalled]=numFnsCalled;
				switch(sorts){
				case 3:valueTableUll[numFnsCalled] = fnInfo->
					       m_timesCalled;
					break;
				case 4:valueTableUll[numFnsCalled] = fnInfo->
					       m_totalTimeTaken;
					break;
				case 5:// sorting float values till the 4th
					// dec place
					valueTableF[numFnsCalled] = ((float)fnInfo->m_totalTimeTaken)/((float)fnInfo->m_timesCalled);
					break;
				case 6:valueTableUll[numFnsCalled] = fnInfo->
					       m_maxTimeTaken;
					break;
				case 7:valueTableUll[numFnsCalled] = fnInfo->
					       m_numCalledFromThread;
					break;
				case 8:valueTableUll[numFnsCalled] = fnInfo->
					       m_maxBlockedTime;
					break;
					//For now for any other value of slot
					// so that we don't error
				default:valueTableUll[numFnsCalled] = fnInfo->
						m_numCalledFromThread;
				}
				numFnsCalled++;
			}
		}
	}
	if (sorts==5)
		gbqsort(indexTable,numFnsCalled,sizeof(uint32_t),
		      decend_cmpF);
	else
		gbqsort(indexTable,numFnsCalled,sizeof(uint32_t),
		      decend_cmpUll);

	//Now print the sorted values
	for (int32_t i=0;i<numFnsCalled;i++){
		slot=m_fn.getSlot(&keyTable[indexTable[i]]);
		fnInfo=(FnInfo *)m_fn.getValueFromSlot(slot);
		//Don't print functions that have not been called
		sb->safePrintf("<tr><td>%"XINT32"</td><td>%s</td><td>%"INT32"</td><td>%"INT32"</td>"
			       "<td>%.4f</td><td>%"INT32"</td><td>%"INT32"</td><td>%"INT32"</td>"
			       "</tr>",
			       keyTable[indexTable[i]],
			       fnInfo->m_fnName,
			       fnInfo->m_timesCalled,
			       fnInfo->m_totalTimeTaken,
			       ((float)fnInfo->m_totalTimeTaken)/((float)fnInfo->m_timesCalled),
			       fnInfo->m_maxTimeTaken,
			       fnInfo->m_numCalledFromThread, 
			       fnInfo->m_maxBlockedTime);
	}

	sb->safePrintf("</table><br><br>");



	//Now to print the table of functions called in the last 10 seconds
	sb->safePrintf(  "<center>\n<table %s>\n"
			 "<tr class=hdrow><td colspan=8>"
			 "<center><b>Profiler - Last 10 seconds</b></center>"
			 "</td></tr>\n",TABLE_STYLE);
       	sb->safePrintf("<tr bgcolor=#%s>"
		       "<td><b>Address</b></td><td><b>Function</b></td>",
		       LIGHT_BLUE);
	sb->safePrintf("<td><b><a href=/admin/profiler?sort10=3&c=%s&"
		       ">"
		       "Times Called</a></b></td></td>",coll);
	sb->safePrintf("<td><b><a href=/admin/profiler?sort10=4&c=%s&"
		       ">"
		       "Total Time(msec)</a></b></td></td>",coll);
	sb->safePrintf("<td><b><a href=/admin/profiler?sort10=5&c=%s&"
		       ">"
		       "Avg Time(msec)</b></a></td>",coll);
	sb->safePrintf("<td><b><a href=/admin/profiler?sort10=6&c=%s&"
		       ">"
		       "Max Time(msec)</a></b></td>",coll);
	sb->safePrintf("<td><b><a href=/admin/profiler?sort10=7&c=%s&"
		       ">"
		       "Times From Thread</a></b></td></tr>",coll);
	uint64_t now=gettimeofdayInMillisecondsLocal();
	int32_t numFnsCalled10=0;;
	for(int32_t i=0;i<numFnsCalled;i++){
		uint64_t timesCalled=0;
		uint64_t totalTimeTaken=0;
		uint64_t maxTimeTaken=0;
		uint64_t numCalledFromThread=0;
		//If hashtable is less than 10 secs old, use it
		for(int32_t j=0;j<11;j++){
			if ((now-m_fnTime[i]) < 10000){
				//From the keyTable, we know the keys of the
				// functions that have been called
				slot=m_fnTmp[j].getSlot(&keyTable[i]);
				if (slot>=0){
					fnInfo=(FnInfo *)m_fnTmp[j].
						getValueFromSlot(slot);
					totalTimeTaken += fnInfo->
						m_totalTimeTaken;
					timesCalled += fnInfo->m_timesCalled;
					if( ( fnInfo->m_maxTimeTaken) > 
					   maxTimeTaken )
						maxTimeTaken = fnInfo->
							m_maxTimeTaken;
					numCalledFromThread += fnInfo->
						m_numCalledFromThread;
				}
			}
		}
		//After getting all the info, put it in table for sorting
		//Only print those functions that have been called
		if (timesCalled==0) continue;
		//Simply overwriting all the stuff
		keyTable[numFnsCalled10]=keyTable[i];
		indexTable[numFnsCalled10]=numFnsCalled10;
		switch(sort10){
		case 0:break;
		case 3:valueTableUll[numFnsCalled10]=timesCalled;
			break;
		case 4:valueTableUll[numFnsCalled10]=totalTimeTaken;;
			break;
		case 5:valueTableF[numFnsCalled10]=((float)totalTimeTaken)/((float)timesCalled);
			break;
		case 6:valueTableUll[numFnsCalled10]=maxTimeTaken;
			break;
		//For now for any other value of slot so that we don't error
		default:valueTableUll[numFnsCalled10]=numCalledFromThread;
		}
		numFnsCalled10++;
	}

	if (sort10==5)
		gbqsort(indexTable,numFnsCalled10,sizeof(uint32_t),
		      decend_cmpF);
	else
		gbqsort(indexTable,numFnsCalled10,sizeof(uint32_t),
		      decend_cmpUll);

	for(int32_t i=0;i<numFnsCalled10;i++){
		uint64_t timesCalled=0;		
		uint64_t totalTimeTaken=0;
		uint64_t maxTimeTaken=0;
		uint64_t numCalledFromThread=0;
		//If hashtable is less than 10 secs old, continue
		for(int32_t j=0;j<11;j++){
			if ((now-m_fnTime[i])<10000){
				// From the keyTable, we know the keys of the
				// functions that have been called
				slot=m_fnTmp[j].
					getSlot(&keyTable[indexTable[i]]);
				if (slot>=0){
					fnInfo=(FnInfo *)m_fnTmp[j].
						getValueFromSlot(slot);
					totalTimeTaken += fnInfo->
						m_totalTimeTaken;
					timesCalled += fnInfo->m_timesCalled;
					if( ( fnInfo->m_maxTimeTaken)> 
					    maxTimeTaken )
						maxTimeTaken = fnInfo->
							m_maxTimeTaken;
					numCalledFromThread += fnInfo->
						m_numCalledFromThread;
				}
			}
		}
		//Only print those functions that have been called
		if (timesCalled==0) continue;
		slot=m_fn.getSlot(&keyTable[indexTable[i]]);
		fnInfo=(FnInfo *)m_fn.getValueFromSlot(slot);
		//Don't print functions that have not been called
		sb->safePrintf("<tr><td>%"XINT32"</td><td>%s</td><td>%"INT64"</td>"
			       "<td>%"INT64"</td>"
			       "<td>%.4f</td><td>%"INT64"</td><td>%"INT64"</td></tr>",
			       keyTable[indexTable[i]],
			       fnInfo->m_fnName,
			       timesCalled,
			       totalTimeTaken,
			       ((float)totalTimeTaken)/((float)timesCalled),
			       maxTimeTaken,
			       numCalledFromThread);
	}
	sb->safePrintf("</table><br><br>");

	
	
	mfree(indexTable,numSlotsUsed*sizeof(uint32_t),"ProfilerX");
	mfree(keyTable,numSlotsUsed*sizeof(uint32_t),"ProfilerX");
	if (sorts==5 || sort10==5)
		mfree(valueTableF,
		      numSlotsUsed*sizeof(float),
		      "ProfilerY");
	else
		mfree(valueTableUll,
		      numSlotsUsed*sizeof(uint64_t),
		      "ProfilerY");



	if(qpreset) {
		m_quickpolls.clear();
		m_lastQPUsed = 0;
	}

	numSlots = m_quickpolls.getNumSlots();
	numSlotsUsed = m_quickpolls.getNumSlotsUsed();
	sb->safePrintf("<center>\n<table %s>\n"
		       "<tr class=hdrow><td colspan=5>"
		       "<center><b>Triggered Quickpolls "
		       "<a href=\"/admin/profiler?c=%s"
		       "&qpreset=1\">"
		       "(reset)</a></b></center>"
		       "</td></tr>\n",
		       TABLE_STYLE,
		       coll);

	sb->safePrintf("<tr bgcolor=#%s>"
		       "<td><b>Between Functions</b></td>"
		       "<td><b>max blocked(msec)</b></td>"
		       "<td><b>avg time(msec)</b></td>"
		       "<td><b>times triggered</b></td>"
		       "<td><b>total(msec)</b></td>"
		       "</tr>"
		       , LIGHT_BLUE );

	if(numSlotsUsed == 0) {
		sb->safePrintf("</table>");
		return true;
	}

	valueTableUll = (uint64_t*)
		mcalloc(numSlotsUsed * sizeof(uint64_t),"ProfilerZ");
	if(!valueTableUll) {
		sb->safePrintf("</table>");
		return true;
	}

	indexTable = (uint32_t*)mcalloc(numSlotsUsed * 
					     sizeof(uint32_t),
					     "ProfilerZ");
	if(!indexTable) {
		mfree(indexTable,   
		      numSlotsUsed*sizeof(uint32_t),
		      "ProfilerZ");
		sb->safePrintf("</table>");
		return true;
	}

	keyTable = (uint32_t*)mcalloc(numSlotsUsed * 
					   sizeof(uint32_t),
					   "ProfilerZ");
	if(!keyTable) {
		mfree(indexTable,   
		      numSlotsUsed*sizeof(uint32_t),
		      "ProfilerZ");
		mfree(valueTableUll,
		      numSlotsUsed*sizeof(uint64_t),
		      "ProfilerZ");
		sb->safePrintf("</table>");
		return true;
	}

	int32_t j = 0;
	for (int32_t i = 0; i < numSlots; i++) {
		//if((key = m_quickpolls.getKey(i)) == 0) continue;
		if ( m_quickpolls.isEmpty(i) ) continue;
		QuickPollInfo* q = *(QuickPollInfo **)m_quickpolls.getValueFromSlot(i);
		int32_t took = q->m_maxTime;
		valueTableUll[j] = took;
		indexTable[j] = j; 
		keyTable[j] = i; 
		j++;
	}
	gbqsort(indexTable, j, sizeof(uint32_t), decend_cmpUll);
	
	for (int32_t i = 0; i < numSlotsUsed; i++){
		int32_t slot = keyTable[indexTable[i]];
		//key = m_quickpolls.getKey(slot);
		QuickPollInfo* q = *(QuickPollInfo **)m_quickpolls.getValueFromSlot(slot);
		sb->safePrintf("<tr><td>%s:%"INT32"<br>%s:%"INT32"</td>"
			       "<td>%"INT32"</td>"
			       "<td>%f</td>"
			       "<td>%"INT32"</td>"
			       "<td>%"INT32"</td>"
			       "</tr>",
			       q->m_caller,  q->m_lineno, q->m_last, 
			       q->m_lastlineno,q->m_maxTime,
			       (float)q->m_timeAcc / q->m_times,
			       q->m_times,
			       q->m_timeAcc);
	}
	sb->safePrintf("</table>");

	mfree(valueTableUll,numSlotsUsed*sizeof(uint64_t),"ProfilerZ");
	mfree(indexTable,   numSlotsUsed*sizeof(uint32_t),"ProfilerZ");
	mfree(keyTable,     numSlotsUsed*sizeof(uint32_t),"ProfilerZ");
	return true;
}

//backwards so we get highest scores first.
static int decend_cmpUll ( const void *h1 , const void *h2 ) {
        uint32_t tmp1, tmp2;
        tmp1 = *(uint32_t *)h1;
	tmp2 = *(uint32_t *)h2;
	if (valueTableUll[tmp1]>valueTableUll[tmp2]) {
		return -1;	
	}
        else if(valueTableUll[tmp1]<valueTableUll[tmp2]){
		return 1;
	}
	else return 0;
}

//backwards so we get highest scores first.
static int decend_cmpF ( const void *h1 , const void *h2 ) {
        uint32_t tmp1, tmp2;
        tmp1 = *(uint32_t *)h1;
	tmp2 = *(uint32_t *)h2;
	if (valueTableF[tmp1]>valueTableF[tmp2]) {
		return -1;	
	}
        else if(valueTableF[tmp1]<valueTableF[tmp2]){
		return 1;
	}
	else return 0;
}


char* Profiler::getFnName( PTRTYPE address,int32_t *nameLen){
	FnInfo *fnInfo;
	int32_t slot=m_fn.getSlot(&address);
	if(slot!=-1)
		fnInfo=(FnInfo *)m_fn.getValueFromSlot(slot);
	else 
		return NULL;
	if (nameLen)
	*nameLen=gbstrlen(fnInfo->m_fnName);
	return fnInfo->m_fnName;
}
	
bool sendPageProfiler ( TcpSocket *s , HttpRequest *r ) {
	SafeBuf sb;
	sb.reserve2x(32768);

	

	//read in all of the possible cgi parms off the bat:
	//int32_t  user     = g_pages.getUserType( s , r );
	char *username = g_users.getUsername(r);
	//char *pwd  = r->getString ("pwd");

	char *coll = r->getString ("c");
	int32_t collLen;
	if ( ! coll || ! coll[0] ) {
		//coll    = g_conf.m_defaultColl;
		coll = g_conf.getDefaultColl( r->getHost(), r->getHostLen() );
	}
	collLen = gbstrlen(coll);
	int sorts=(int) r->getLong("sorts",0);
	int sort10=(int)r->getLong("sort10",0);
	int qpreset=(int)r->getLong("qpreset",0);
	int profilerreset=(int)r->getLong("profilerreset",0);
	int realTimeSortMode=(int)r->getLong("rtsort",2);
	int realTimeShowAll=(int)r->getLong("rtall",0);
	int startRt=(int)r->getLong("rtstart",0);
	int stopRt=(int)r->getLong("rtstop",0);
	
	g_pages.printAdminTop ( &sb , s , r );

	// no permmission?
	bool isMasterAdmin = g_conf.isMasterAdmin ( s , r );
	bool isCollAdmin = g_conf.isCollAdmin ( s , r );
	if ( ! isMasterAdmin &&
	     ! isCollAdmin ) {
		//g_errno = ENOPERM;
		//g_httpServer.sendErrorReply(s,g_errno,mstrerror(g_errno));
		//return true;
		sorts = 0;
		sort10 = 0;
		qpreset = 0;
		profilerreset = 0;
		realTimeSortMode = 2;
		realTimeShowAll = 0;
		startRt = 0;
		stopRt = 0;
	}


	
	if (!g_conf.m_profilingEnabled)
		sb.safePrintf("<font color=#ff0000><b><centeR>"
			      "Sorry, this feature is temporarily disabled. "
			      "Enable it in MasterControls.</center></b></font>");
	else {
		if(g_profiler.m_realTimeProfilerRunning) {
			if(stopRt) {
				g_profiler.stopRealTimeProfiler();
				g_profiler.m_ipBuf.purge();
			}
		} else if(startRt)   g_profiler.startRealTimeProfiler();
				
		g_profiler.printRealTimeInfo(&sb,
					     username,
					     NULL,
					     coll,
					     realTimeSortMode,
					     realTimeShowAll);
		// g_profiler.printInfo(&sb,username,NULL,coll,sorts,sort10, qpreset,
		// 		     profilerreset);
	}

	return g_httpServer.sendDynamicPage ( s , (char*) sb.getBufStart() ,
						sb.length() ,-1 , false);
}

FrameTrace *
FrameTrace::set(const uint32_t addr) {
	address = addr;
	return this;
}

FrameTrace *
FrameTrace::add(const uint32_t addr) {
	//log("add %x", addr);
	// We should be finding children most of the time not adding them.
	int32_t left = 0;
	int32_t right = m_numChildren - 1;
	while(left <= right) {
		const int32_t middle = (left + right) >> 1;
		FrameTrace *frame = m_children[middle];
		if(frame->address == addr) {
			//log("found %x %x", addr, frame);
			return frame;
		}
		if(frame->address < addr) {
			left = middle + 1;
		}
		else {
			right = middle - 1;
		}
	}
	if(m_numChildren == MAX_CHILDREN) {
		log("profiler: Relatime profiler frame trace node full!");
		// This node is full.
		return NULL;
	}
	// Did not find it, add it
	FrameTrace *frame = g_profiler.getNewFrameTrace(addr);
	if(!frame) {
		// Our static buffer must be used up.
		return NULL;
	}
	memmove( 	m_children + left + 1,
			m_children + left,
			sizeof(FrameTrace *) * (m_numChildren - left));
	m_children[left] = frame;
	++m_numChildren;
	return frame;
}

void
FrameTrace::dump(	SafeBuf *out,
			const uint32_t level,
			uint32_t printStart) const {
	if(level) {
		char *name = g_profiler.getFnName(address);
		out->pad(' ', level);
		uint32_t l;
		if(name && (l = gbstrlen(name))) {
			out->safePrintf("%s ", name);
		} else {
			l = sizeof("Unknown ") - 2;
			out->safePrintf("Unknown ");
		}
		if(hits) {
			out->pushChar(' ');
			out->pad('.', printStart - level - l - 3);
			out->pushChar('|');
			out->safePrintf(" %-10i", hits);
			out->pushChar('|');
		} else {
			out->pad(' ', printStart - level - l - 2);
			out->safePrintf("|           |");
		}
		if(missQuickPoll) {
			out->safePrintf("  %-10i    |", missQuickPoll);
		} else {
			out->safePrintf("                |");
		}
		out->safePrintf(" %#.8x |", address);
		out->safePrintf("\n");
	} else {
		out->safePrintf("|Stack Trace");
		printStart = getPrintLen(0) + 3;
		out->pad(' ', printStart - sizeof("|Stack Trace"));
		out->safePrintf("| Hits      |Missed QUICKPOLL| Address    |\n");
		out->pad('-', printStart + sizeof("| Hits      |Missed QUICKPOLL| Address    |") - 2);
		out->safePrintf("\n");
	}
	for(uint32_t i = 0; i < m_numChildren; ++i) 
		m_children[i]->dump(out, level + 2, printStart);
}

uint32_t
FrameTrace::getPrintLen(const uint32_t level) const {
	uint32_t ret = level;
	if(level) {
		char *name = g_profiler.getFnName(address);
		uint32_t l;
		if(!(name && (l = gbstrlen(name)))) {
			l = sizeof("Unknown");
		}
		ret += l;
	}
	for(uint32_t i = 0; i < m_numChildren; ++i) {
		const uint32_t l = m_children[i]->getPrintLen(level + 2);
		if(l > ret) ret = l;
	}
	return ret;
}

FrameTrace *
Profiler::getFrameTrace(void **trace, const uint32_t numFrames) {
	FrameTrace *frame = g_profiler.m_rootFrame;
	if ( ! frame ) {
		log("profiler: profiler frame was null");
		return NULL;
	}
	for(uint32_t i = numFrames - 3; i > 1; --i) {
		uint32_t base =
			g_profiler.getFuncBaseAddr((PTRTYPE)trace[i]);
		frame = frame->add(base);
		if(!frame) return NULL;
	}
	return frame;
}

FrameTrace *
Profiler::updateRealTimeData( 	void **trace,
				const uint32_t numFrames,
				uint32_t **ptr) {
	// Find or create and set of stack frames which match this one.
	FrameTrace *frame = getFrameTrace(trace, numFrames);
	if(frame) ++frame->hits;
	PTRTYPE stackPtr = (PTRTYPE)trace[2];
	//*ptr = (uint32_t *)realTimeProfilerData.getValuePointer(stackPtr);
	*ptr = (uint32_t *)realTimeProfilerData.getValue(&stackPtr);
	uint64_t newHit = 1;
	//if(!*ptr) realTimeProfilerData.addKey(stackPtr, newHit);
	if(!*ptr) realTimeProfilerData.addKey(&stackPtr, &newHit);
	else ++*ptr[0];
	return frame;
}


// void
// Profiler::addMissedQuickPoll( ) {
// 	void *trace[32];
// 	uint32_t numFrames = backtrace(trace, 32);
// 	if(numFrames < 3) return;
// 	const void *stackPtr = trace[2];
// 	uint32_t baseAddress = g_profiler.getFuncBaseAddr((uint32_t)stackPtr);
// 	uint32_t *ptr;
// 	FrameTrace *frame = updateRealTimeData(trace, numFrames, &ptr);
// 	if(frame) ++frame->missQuickPoll;
// 	if(!ptr) ptr = (uint32_t *)realTimeProfilerData.getValuePointer(
// 							   uint32_t(stackPtr));
// 	if(ptr) ++ptr[1];
// }


void
Profiler::checkMissedQuickPoll( FrameTrace *frame,
				const uint32_t stackPtr,
				uint32_t *ptr)
{
	if(g_niceness == 0 || // !g_loop.m_canQuickPoll || 
	   !g_loop.m_needsToQuickPoll ||
	   g_loop.m_inQuickPoll) return;	
	// Get the time difference from when the function we last saw was
	// different. The odds are very good that this time represents the time
	// which has been spent in this function.
// 	const uint64_t time = gettimeofdayInMilliseconds();
// 	const uint64_t delta = time - g_profiler.m_lastDeltaAddressTime;
// 	const uint32_t maxDelta = QUICKPOLL_INTERVAL;
	//log("delta %i", delta);
	//	if(delta <= maxDelta) return;
	//	if(time - g_loop.m_lastPollTime <= maxDelta) return;
	// If it got here then there is a good change a quick poll call needs to
	// be added. 
	if(frame) ++frame->missQuickPoll;
	//if(!ptr) ptr =(uint32_t *)realTimeProfilerData.getValuePointer(
	//			uint32_t(stackPtr));
	if(!ptr) ptr = (uint32_t *)realTimeProfilerData.getValue((uint32_t *)
								 &stackPtr);
	if(ptr) ++ptr[1];
}

// from gb-include.h
extern int g_inMemcpy;

void
Profiler::getStackFrame(int sig) {

	// need to interrupt every 1ms to set this to true
	g_clockNeedsUpdate = true;

	// profile once every 5ms, not every 1ms
	static int32_t s_count = 0;

	// turn off after 60 seconds of profiling
	if ( m_totalFrames++ >= 60000 ) {
		stopRealTimeProfiler(false);
		return;
	}

	if ( ++s_count != 5 ) return;

	s_count = 0;


	// prevent cores.
	// TODO: hack this to a function somehow...
	// we set this to positive values when calling library functions like
	// zlib's inflate/deflate that call memcpy() so we can't measure
	// those in the profiler unfortunately unless we put a hack in here
	// somewhere. but for now just ignore.
	if ( g_inMemcpy ) return;

	// likewise, not if in system malloc since backtrace() mallocs
	if ( g_inMemFunction ) return;

	//void *trace[32];

	// the innermost line number
	// if ( g_profiler.m_ipLineBuf.m_length + 8 >= 
	//      g_profiler.m_ipLineBuf.m_capacity )
	// 	return;

	// the lines calling functions
	// if ( g_profiler.m_ipPathBuf.m_length + 8*32 >= 
	//      g_profiler.m_ipPathBuf.m_capacity )
	// 	return;

	// the lines calling functions
	if ( g_profiler.m_ipBuf.m_length + 8*32 >= 
	     g_profiler.m_ipBuf.m_capacity )
	 	return;

	// support 64-bit
	void *trace[32];
	int32_t numFrames = backtrace((void **)trace, 32);
	if(numFrames < 3) return;

	// the individual line for profiling the worst individual functions.
	// we'll have to remove line #'s from the output of addr2line
	// using awk i guess.
	//g_profiler.m_ipLineBuf.pushLongLong((uint64_t)trace[2]);

	// . now just store the Instruction Ptrs into a count hashtable
	// . skip ahead 2 to avoid the sigalrm function handler
	for ( int32_t i = 2 ; i < numFrames  ; i++ ) {

		// even if we are 32-bit, make this 64-bit for ease
		uint64_t addr = (uint64_t)(PTRTYPE)trace[i];

		//if ( addr > 0xf0000000 )
		//log("profiler: %i) addr = %llx",i,(unsigned long long)addr);

		// the call stack path for profiling the worst paths
		g_profiler.m_ipBuf.pushLongLong(addr);
		continue;
		// just store lowest addr for now
		//break;
		/*
		int32_t slot = g_profiler.m_ipCountTable.getSlot ( &addr );
		if ( slot < 0 ) {
			int32_t val = 1;
			g_profiler.m_ipCountTable.addKey ( &addr , &val );
			continue;
		}
		// update existing
		uint32_t *val;
		val=(uint32_t *)g_profiler.
			m_ipCountTable.getValueFromSlot(slot);
		*val = *val + 1;
		*/
	}

	// a secret # to indicate missed quickpoll
	if ( g_niceness != 0 &&
	     g_loop.m_needsToQuickPoll &&
	     ! g_loop.m_inQuickPoll )
		g_profiler.m_ipBuf.pushLongLong(0x123456789LL);

	// indicate end of call stack path
	g_profiler.m_ipBuf.pushLongLong(0LL);//addr);


	return;

	/*
	// if we are in need of a quickpoll store it here
	if( g_niceness == 0 ) return;
	// !g_loop.m_canQuickPoll || 
	if ( ! g_loop.m_needsToQuickPoll ) return;
	if ( g_loop.m_inQuickPoll ) return;

	// do not breach
	if ( g_profiler.m_quickpollMissBuf.m_length + 8*32 >= 
	     g_profiler.m_quickpollMissBuf.m_capacity )
		return;

	// store address in need of quickpolls
	//g_profiler.m_quickpollMissBuf.
	//	pushLongLong((uint64_t)trace[2] );
	for ( int32_t i = 2 ; i < numFrames && i <= 4 ; i++ ) {
		// even if we are 32-bit, make this 64-bit for ease
		uint64_t addr = (uint64_t)trace[i];
		// the call stack path for profiling the worst paths
		g_profiler.m_quickpollMissBuf.pushLongLong(addr);
	}

	// all done
	return;
	*/

	const void *stackPtr = trace[2];
	uint32_t baseAddress = g_profiler.getFuncBaseAddr((PTRTYPE)stackPtr);
	uint32_t *ptr;	
	FrameTrace *frame = updateRealTimeData(trace, numFrames, &ptr);
	if(baseAddress != g_profiler.m_lastDeltaAddress) {
		// This function is different from the last function we saw
		g_profiler.m_lastDeltaAddressTime =gettimeofdayInMilliseconds();
		g_profiler.m_lastDeltaAddress = baseAddress;
	}
	checkMissedQuickPoll( 	frame,
				(PTRTYPE)stackPtr,
				ptr);
}

void
Profiler::startRealTimeProfiler() {
	log(LOG_INIT, "admin: starting real time profiler");
	// if(!m_frameTraces) {
	// 	m_frameTraces = (FrameTrace *)mmalloc(
	// 		sizeof(FrameTrace) * MAX_FRAME_TRACES, "FrameTraces");
	// 	memset(m_frameTraces, 0, sizeof(FrameTrace) * MAX_FRAME_TRACES);
	// 	m_numUsedFrameTraces = 0;
	// 	m_rootFrame = &m_frameTraces[m_numUsedFrameTraces++];
	// }
	init();
	m_realTimeProfilerRunning = true;
	m_totalFrames = 0;
	// now Loop.cpp will call g_profiler.getStackFrame()
	return;

	struct itimerval value, ovalue;
	int which = ITIMER_REAL;
	//signal(SIGVTALRM, Profiler::getStackFrame);
	//signal(SIGALRM, Profiler::getStackFrame);
	value.it_interval.tv_sec = 0;
	// 1000 microseconds is 1 millisecond
	value.it_interval.tv_usec = 1000;
	value.it_value.tv_sec = 0;
	value.it_value.tv_usec = 1000;
	setitimer( which, &value, &ovalue );
}

void
Profiler::stopRealTimeProfiler(const bool keepData) {
	log(LOG_INIT, "admin: stopping real time profiler");
	m_realTimeProfilerRunning = false;

	return;


	struct itimerval value;
	int which = ITIMER_REAL;
	// call the handler in Loop.cpp again
	//signal(SIGALRM,sigalrmHandler);
	getitimer( which, &value );
	value.it_value.tv_sec = 0;
	value.it_value.tv_usec = 0;
	setitimer( which, &value, NULL );
	if(!keepData && m_frameTraces) {
		mfree( 	m_frameTraces,
			sizeof(FrameTrace) * MAX_FRAME_TRACES,
			"FrameTraces");
		m_rootFrame = NULL;
		m_frameTraces = NULL;
	}
}

uint32_t
Profiler::getFuncBaseAddr(const uint32_t address) {
	int32_t low = -1;
	int32_t high = (int32_t)m_lastAddressMapIndex;
	int32_t probe;
	// @@@ MLT Interpolation might be faster.
	while((high - low) > 1) {
		probe = (low + high) >> 1;
		if(m_addressMap[probe] <= address)
			low = probe;
		else
			high = probe;
	}
	return m_addressMap[low];
}

uint32_t
Profiler::getFuncBaseAddr(const char *funcName) {
	for(int32_t i = 0; i < m_fn.getNumSlots(); ++i) {
		if ( m_fn.isEmpty(i) ) continue;
		uint32_t key = *(uint32_t *)m_fn.getKey(i);
		FnInfo *info = (FnInfo *)m_fn.getValueFromSlot(i);
		if(!info || strcmp(info->m_fnName, funcName)) continue;
		return key;
	}
	return 0;
}

int
Profiler::rtDataLocationCmp(const void *A, const void *B) {
	const HitEntry * a = (const HitEntry *)A;
	const HitEntry * b = (const HitEntry *)B;
	if(a->fileHash > b->fileHash) return 1;
	else if(a->fileHash < b->fileHash) return -1;
	if(a->line > b->line) return 1;
	else if(a->line < b->line) return -1;
	return 0;
}

int
Profiler::rtHitCmp(const void *A, const void *B) {
	const HitEntry * a = (const HitEntry *)A;
	const HitEntry * b = (const HitEntry *)B;
	if(a->numHits > b->numHits) return -1;
	else if(a->numHits < b->numHits) return 1;
	return 0;
}

int
Profiler::rtFuncHitCmp(const void *A, const void *B) {
	const HitEntry *a = (const HitEntry *)A;
	const HitEntry *b = (const HitEntry *)B;
	if(a->numHitsPerFunc > b->numHitsPerFunc) return -1;
	else if(a->numHitsPerFunc < b->numHitsPerFunc) return 1;
	return Profiler::rtHitCmp(A, B);
}

int
Profiler::rtAddressCmp(const void *A, const void *B) {
	const HitEntry * a = (const HitEntry *)A;
	const HitEntry * b = (const HitEntry *)B;
	if(a->address > b->address) return -1;
	else if(a->address < b->address) return 1;
	return 0;
}

int
Profiler::rtMissedQuickPolls(const void *A, const void *B) {
	const HitEntry * a = (const HitEntry *)A;
	const HitEntry * b = (const HitEntry *)B;
	if(a->missedQuickPolls > b->missedQuickPolls) return -1;
	else if(a->missedQuickPolls < b->missedQuickPolls) return 1;
	return 0;
}

int
Profiler::rtMissedQuickPollPerFunc(const void *A, const void *B) {
	const HitEntry * a = (const HitEntry *)A;
	const HitEntry * b = (const HitEntry *)B;
	if(a->missedQuickPollsPerFunc > b->missedQuickPollsPerFunc) return -1;
	else if(a->missedQuickPollsPerFunc < b->missedQuickPollsPerFunc)
		return 1;
	return Profiler::rtMissedQuickPolls(A, B);
}

void
Profiler::sortRealTimeData(const uint8_t type, const uint32_t num) {
	// MLT: if too slow then sort using pointers.
	switch(type) {
		case 0:
			gbqsort(hitEntries, num, sizeof(HitEntry),
				&Profiler::rtMissedQuickPollPerFunc);
			break;
		case 1:
			gbqsort(hitEntries, num, sizeof(HitEntry),
				&Profiler::rtAddressCmp);
			break;
		case 2:
			gbqsort(hitEntries, num, sizeof(HitEntry),
				&Profiler::rtFuncHitCmp);
			break;
	}
}


class PathBucket {
public:
	char *m_pathStackPtr;
	int32_t m_calledTimes;
	int32_t m_missedQuickPolls;
};
       

int cmpPathBucket (const void *A, const void *B) {
	const PathBucket *a = *(const PathBucket **)A;
	const PathBucket *b = *(const PathBucket **)B;
	if      ( a->m_calledTimes < b->m_calledTimes ) return  1;
	else if ( a->m_calledTimes > b->m_calledTimes ) return -1;
	return 0;
}

bool
Profiler::printRealTimeInfo(SafeBuf *sb,
			    //int32_t user,
			    char *username,
			    char *pwd,
			    char *coll,
			    int realTimeSortMode,
			    int realTimeShowAll) {
	if(!m_realTimeProfilerRunning) {
		sb->safePrintf("<table %s>",TABLE_STYLE);
		sb->safePrintf("<tr class=hdrow><td colspan=7>"
			 "<center><b>Real Time Profiler "
			 "<a href=\"/admin/profiler?c=%s"
			 "&rtstart=1\">"
			 "(Start)</a></b></center>"
			       "</td></tr>\n",coll);
		sb->safePrintf("</table><br><br>\n");
		return true;
	}
	stopRealTimeProfiler(true);

	/*
	if(hitEntries)
		mfree(hitEntries, sizeof(HitEntry)*rtNumEntries, "hitEntries");
	//rtNumEntries = 0;
	//for(int32_t i = 0; i < realTimeProfilerData.getNumSlots(); ++i) {
	//	uint32_t key = realTimeProfilerData.getKey(i);
	//	if(realTimeProfilerData.getValuePointer(key))
	//		++rtNumEntries;
	//}
	rtNumEntries = realTimeProfilerData.getNumUsedSlots();
	if(!rtNumEntries) {
		sb->safePrintf("<table %s>",TABLE_STYLE);
		sb->safePrintf("<tr class=hdrow><td colspan=7>"
			 "<center><b>Real Time Profiler started, refresh page "
			 "after some time."
			 "<a href=\"/admin/profiler?c=%s"
			 "&rtstop=1\">"
			 "(Stop)</a></b></center>"
			       "</td></tr>\n",coll);
		sb->safePrintf("</table><br><br>\n");
		startRealTimeProfiler();
		return true;
	}
	*/


	sb->safePrintf("<table %s>",TABLE_STYLE);
	// char *showMessage;
	// int rtall;
	// if(realTimeShowAll) {
	// 	showMessage = "(show only 10)";
	// 	rtall = 0;
	// } else {
	// 	showMessage = "(show all)";
	// 	rtall = 1;
	// }
	sb->safePrintf("<tr class=hdrow>"
		       "<td colspan=7>"
			 "<b>Top 100 Profiled Line Numbers "
		       //"<a href=\"/admin/profiler?c=%s"
		       // "&rtall=%i\">%s</a>"
		       //,coll,
		       // rtall, showMessage);
		       );
	sb->safePrintf(
		       // "<a href=\"/admin/profiler?c=%s&rtstop=1\">"
		       // "(Stop)</a> [Click refresh to get latest profile "
		       // "stats][Don't forget to click STOP when done so you "
		       // "don't leave the profiler running which can slow "
		       //"things down.]"
		       "</b>"
		       "</td></tr>\n"
		       //,coll
		       );
	/*
	rtall = !rtall;

	sb->safePrintf("<tr><td><b>"
		       "Function</b></td>");

	sb->safePrintf("<td><b><a href=/admin/profiler?rtsort=2&c=%s&"
		       "&rtall=%i>"
		       "Hits per Func</b></a></td>",coll,rtall);

	sb->safePrintf("<td><b><a href=/admin/profiler?rtsort=0&c=%s&"
		       "&rtall=%i>"
		       "Missed QUICKPOLL calls<br>per Func</b></a></td>",
		       coll,rtall);

	sb->safePrintf("<td><b><a href=/admin/profiler?rtsort=1&c=%s&"
		       "&rtall=%i>"
		       "Base Address</b></a></td>",coll,rtall);

	sb->safePrintf("<td><b>Hits per Line</b></td>"

		       "<td><b>Line Address</b></td>"

		       "<td><b>Missed QUICKPOLL calls<br>"
		       "per Line</b></td></tr>");
	*/

	// system call to get the function names and line numbers
	// just dump the buffer
	char *ip = (char *)m_ipBuf.getBufStart();
	char *ipEnd = (char *)m_ipBuf.getBuf();
	SafeBuf ff;
	ff.safePrintf("%strash/profile.txt",g_hostdb.m_dir);
	char *filename = ff.getBufStart();
	unlink ( filename );
	int fd = open ( filename , O_RDWR | O_CREAT , getFileCreationFlags() );
	if ( fd < 0 ) {
		sb->safePrintf("FAILED TO OPEN %s for writing: %s"
			       ,ff.getBufStart(),mstrerror(errno));
		return false;
	}
	for ( ; ip < ipEnd ; ip += sizeof(uint64_t) ) {
		// 0 marks end of call stack
		if ( *(long long *)ip == 0 ) continue;
		char tmp[64];
		int tlen = sprintf(tmp, "0x%llx\n", *(long long *)ip);
		int nw = write ( fd , tmp , tlen );
		if ( nw != tlen )
			log("profiler: write failed");
	}
	::close(fd);
	SafeBuf cmd;
	SafeBuf newf;
	newf.safePrintf("%strash/output.txt",g_hostdb.m_dir);
	// print the addr again somehow so we know
	cmd.safePrintf("addr2line  -a -s -p -C -f -e ./gb < %s | "
		       "sort | uniq -c | sort -rn > %s"
		       ,filename,newf.getBufStart());
	gbsystem ( cmd.getBufStart() );

	SafeBuf out;
	out.load ( newf.getBufStart());

	// restrict to top 100 lines
	char *x = out.getBufStart();

	if ( ! x ) {
		sb->safePrintf("FAILED TO READ trash/output.txt: %s"
			       ,mstrerror(g_errno));
		return false;
	}

	int lineCount = 0;
	for ( ; *x ; x++ ) {
		if ( *x != '\n' ) continue;
		if ( ++lineCount >= 100 ) break;
	}
	char c = *x;
	*x = '\0';

	sb->safePrintf("<tr><td colspan=10>"
		       "<pre>"
		       "%s"
		       "</pre>"
		       "</td>"
		       "</tr>"
		       "</table>"
		       , out.getBufStart() 
		       );

	*x = c;

	// now each function is in outbuf with the addr, so make a map
	// and use that map to display the top paths below. we hash the addrs
	// in each callstack together and the count those hashes to get
	// the top winners. and then convert the top winners to the
	// function names.
	char *p = out.getBufStart();
	HashTableX map;
	map.set ( 8,8,1024,NULL,0,false,0,"pmtb");
	for ( ; *p ; ) {
		// get addr
		uint64_t addr64;
		sscanf ( p , "%*i %"XINT64" ", &addr64 );
		// skip if 0
		if ( addr64 ) {
			// record it
			int64_t off = p - out.getBufStart();
			map.addKey ( &addr64 , &off );
		}
		// skip to next line
		for ( ; *p && *p !='\n' ; p++ );
		if ( *p ) p++;
	}

	// now scan m_ipBuf (Instruction Ptr Buf) and make the callstack hashes
	ip = (char *)m_ipBuf.getBufStart();
	ipEnd = (char *)m_ipBuf.getBuf();
	char *firstOne = NULL;
	bool missedQuickPoll = false;
	uint64_t hhh = 0LL;
	HashTableX pathTable;
	pathTable.set ( 8,sizeof(PathBucket),1024,NULL,0,false,0,"pbproftb");
	for ( ; ip < ipEnd ; ip += sizeof(uint64_t) ) {
		if ( ! firstOne ) firstOne = ip;
		uint64_t addr64 = *(uint64_t *)ip;
		// this means a missed quickpoll
		if ( addr64 == 0x123456789LL ) {
			missedQuickPoll = true;
			continue;
		}
		// end of a stack
		if ( addr64 != 0LL ) { 
			hhh ^= addr64;
			continue;
		}
		// remove the last one though, because that is the line #
		// of the innermost function and we don't want to include it
		//hhh ^= lastAddr64;
		// it's the end, so add it into table
		PathBucket *pb = (PathBucket *)pathTable.getValue ( &hhh );
		if ( pb ) {
			pb->m_calledTimes++;
			if ( missedQuickPoll )
				pb->m_missedQuickPolls++;
			firstOne = NULL;
			hhh = 0LL;
			missedQuickPoll = false;
			continue;
		}
		// make a new one
		PathBucket npb;
		npb.m_pathStackPtr = firstOne;
		npb.m_calledTimes  = 1;
		if ( missedQuickPoll ) npb.m_missedQuickPolls = 1;
		else 		       npb.m_missedQuickPolls = 0;
		pathTable.addKey ( &hhh , &npb );
		// start over for next path
		firstOne = NULL;
		hhh = 0LL;
		missedQuickPoll = false;
	}

	// now make a buffer of pointers to the pathbuckets in the table
	SafeBuf sortBuf;
	for ( int32_t i = 0 ; i < pathTable.m_numSlots ; i++ ) {
		// skip empty slots
		if ( ! pathTable.m_flags[i] ) continue;
		// get the bucket
		PathBucket *pb = (PathBucket *)pathTable.getValueFromSlot(i);
		// store the ptr
		sortBuf.safeMemcpy ( &pb , sizeof(PathBucket *) );
	}
	// now sort it up
	int32_t count = sortBuf.length() / sizeof(PathBucket *);
	qsort(sortBuf.getBufStart(),count,sizeof(PathBucket *),cmpPathBucket);


	// show profiled paths
	sb->safePrintf("<br><br><table %s>",TABLE_STYLE);
	sb->safePrintf("<tr class=hdrow>"
		       "<td colspan=7>"
		       "<b>Top 50 Profiled Paths</b>"
		       "</td></tr>"
		       "<tr><td colspan=10><pre>");

	// now print the top 50 out
	char *sp = sortBuf.getBufStart();
	char *spend = sp + sortBuf.length();
	int toPrint = 50;
	for ( ; sp < spend && toPrint > 0 ; sp += sizeof(PathBucket *) ) {
		toPrint--;
		PathBucket *pb = *(PathBucket **)sp;
		// get the callstack into m_ipBuf
		uint64_t *cs = (uint64_t *)pb->m_pathStackPtr;
		// scan those
		for ( ; *cs ; cs++ ) {
			// lookup this addr
			long *outOffPtr = (long *)map.getValue ( cs );
			if ( ! outOffPtr ) { 
				sb->safePrintf("        [0x%"XINT64"]\n",*cs);
				continue;
			}
			// print that line out until \n
			char *a = out.getBufStart() + *outOffPtr;
			for ( ; *a && *a != '\n' ; a++ )
				sb->pushChar(*a);
			sb->pushChar('\n');
		}
		// the count
		sb->safePrintf("<b>%i</b>",(int)pb->m_calledTimes);

		if ( pb->m_missedQuickPolls )
			sb->safePrintf(" <b><font color=red>(missed "
				       "%i quickpolls)</font></b>",
				       (int)pb->m_missedQuickPolls);

		sb->safePrintf("\n-----------------------------\n");
	}
	

	sb->safePrintf("</pre></td></tr></table>");


	/*
	for ( int i = 0 ; i < m_ipCountTable.m_numSlots ; i++ ) {
		if ( ! m_ipCountTable.m_flags[i] ) continue;
		int32_t *count = (int32_t *)m_ipCountTable.getValueFromSlot(i);
		uint64_t *addr = (uint64_t *)m_ipCountTable.getKeyFromSlot(i);
		// show the row
		sb->safePrintf("<tr>"
			       "<td>0x%llx</td>" // func
			       "<td>%i</td>" // hits
			       "<td>%i</td>" // missed quickpolls
			       "<td>%i</td>" // base addr
			       "<td>%i</td>" // hits per line
			       "<td>%i</td>" // line addr
			       "<td>%i</td>" // missed quickpoll calls
			       
			       ,(long long)*addr
			       ,*count
			       , 0
			       , 0
			       , 0
			       , 0
			       , 0
			       );
	}
	*/


	/*
	// do new missed quickpolls
	sb->safePrintf("<br><br><table %s>",TABLE_STYLE);
	sb->safePrintf("<tr class=hdrow>"
		       "<td colspan=7>"
		       "<b>Top 100 Missed QuickPolls "
		       );
	ip = (char *)m_quickpollMissBuf.getBufStart();
	ipEnd = (char *)m_quickpollMissBuf.getBuf();
	ff.reset();
	ff.safePrintf("%strash/qp.txt",g_hostdb.m_dir);
	filename = ff.getBufStart();
	//fd = open ( filename , O_RDWR | O_CREAT , S_IRWXU );
	if ( fd < 0 ) {
		sb->safePrintf("FAILED TO OPEN %s for writing: %s"
			       ,ff.getBufStart(),strerror(errno));
		return false;
	}
	for ( ; ip < ipEnd ; ip += sizeof(uint64_t) ) {
		// 0 marks end of call stack
		if ( *(long long *)ip == 0 ) continue;
		char tmp[64];
		int tlen = sprintf(tmp, "0x%llx\n", *(long long *)ip);
		int nw = write ( fd , tmp , tlen );
		if ( nw != tlen )
			log("profiler: write failed");
	}
	::close(fd);
	cmd.reset();
	newf.reset();
	newf.safePrintf("%strash/output.txt",g_hostdb.m_dir);
	// print the addr again somehow so we know
	cmd.safePrintf("addr2line  -a -s -p -C -f -e ./gb < %s | "
		       "sort | uniq -c | sort -rn > %s"
		       ,filename,newf.getBufStart());
	gbsystem ( cmd.getBufStart() );
	out.reset();
	out.load ( newf.getBufStart());
	x = out.getBufStart();
	lineCount = 0;
	for ( ; *x ; x++ ) {
		if ( *x != '\n' ) continue;
		if ( ++lineCount >= 100 ) break;
	}
	c = *x;
	*x = '\0';
	sb->safePrintf("<tr><td colspan=10>"
		       "<pre>"
		       "%s"
		       "</pre>"
		       "</td>"
		       "</tr>"
		       "</table>"
		       , out.getBufStart() 
		       );

	*x = c;
	*/


	// just leave it off if we printed something. but if we just
	// turn the profiler on then m_ipBuf will be empty so start it
	if ( m_ipBuf.length() == 0 )
		g_profiler.startRealTimeProfiler();	

	return true;
		/*

	hitEntries = (HitEntry *)mmalloc(sizeof(HitEntry) * rtNumEntries,
					"hiEntries");
	memset(hitEntries, 0, sizeof(HitEntry) * rtNumEntries);
	uint32_t index = 0;
	for(int32_t i = 0; i < realTimeProfilerData.getNumSlots(); ++i) {
		//if(!realTimeProfilerData.getValuePointer(key))
		//	continue;
		if ( realTimeProfilerData.isEmpty(i) ) continue;
		uint32_t key = *(uint32_t *)realTimeProfilerData.getKey(i);
		HitEntry &entry = hitEntries[index++];
		//const uint32_t *ptr =
		//	(uint32_t *)realTimeProfilerData.getValuePointer(key);
		uint32_t *ptr = (uint32_t *)realTimeProfilerData.
			getValueFromSlot(i);
		entry.numHits = ptr[0];
		entry.numHitsPerFunc = 0;
		entry.baseAddress = getFuncBaseAddr(key);
		entry.funcName = getFnName(entry.baseAddress);
		entry.line = 0;
		entry.file = NULL;
		entry.address = key;
		entry.missedQuickPolls = ptr[1];
	}
	sortRealTimeData(1, index);
	uint32_t lastBaseAddress = hitEntries[0].baseAddress;
	uint32_t lastChangeIndex = 0;
	uint32_t hitCount = 0;
	uint32_t missedQuickPolls = 0;
	for(uint32_t i = 0; i < index; ++i) {
		const HitEntry &entry = hitEntries[i];
		if(entry.baseAddress == lastBaseAddress) {
			hitCount += entry.numHits;
			missedQuickPolls += entry.missedQuickPolls;
			continue;
		}
		for(uint32_t j = lastChangeIndex; j < i; ++j) {
			hitEntries[j].numHitsPerFunc = hitCount;
			hitEntries[j].missedQuickPollsPerFunc=missedQuickPolls;
		}
		lastChangeIndex = i;
		hitCount = entry.numHits;
		missedQuickPolls = entry.missedQuickPolls;
		lastBaseAddress = entry.baseAddress;
	}
	if(hitCount) {
		for(uint32_t i = lastChangeIndex; i < index; ++i) {
			hitEntries[i].numHitsPerFunc = hitCount;
			hitEntries[i].missedQuickPollsPerFunc=missedQuickPolls; 
		}
	}
	sb->safePrintf("<table %s>",TABLE_STYLE);
	char *showMessage;
	int rtall;
	if(realTimeShowAll) {
		showMessage = "(show only 10)";
		rtall = 0;
	} else {
		showMessage = "(show all)";
		rtall = 1;
	}
	sb->safePrintf("<tr class=hdrow><td colspan=7>"
			 "<center><b>Real Time Profiler "
			 "<a href=\"/admin/profiler?c=%s"
			 "&rtall=%i\">%s</a>"
		       ,coll,
			 rtall, showMessage);
	sb->safePrintf("<a href=\"/admin/profiler?c=%s&rtstop=1\">"
		       "(Stop)</a></b></center></td></tr>\n",
		       coll);
	rtall = !rtall;
	sb->safePrintf("<tr><td><b>"
		       "Function</b></td>");
	sb->safePrintf("<td><b><a href=/admin/profiler?rtsort=2&c=%s&"
		       "&rtall=%i>"
		       "Hits per Func</b></a></td>",coll,rtall);
	sb->safePrintf("<td><b><a href=/admin/profiler?rtsort=0&c=%s&"
		       "&rtall=%i>"
		       "Missed QUICKPOLL calls<br>per Func</b></a></td>",
		       coll,rtall);
	sb->safePrintf("<td><b><a href=/admin/profiler?rtsort=1&c=%s&"
		       "&rtall=%i>"
		       "Base Address</b></a></td>",coll,rtall);
	sb->safePrintf("<td><b>Hits per Line</b></td><td>"
		       "<b>Line Address</b></td>"
		       "<td><b>Missed QUICKPOLL calls<br>"
		       "per Line</b></td></tr>");
	sortRealTimeData(realTimeSortMode, index);
	const HitEntry &entry = hitEntries[0];
	lastBaseAddress = entry.baseAddress;
	uint32_t maxEntries;
	if(realTimeShowAll) maxEntries = UINT_MAX;
	else                maxEntries = 25;
	for(uint32_t i = 0; i < index; ++i) {
		top:	
		const HitEntry &entry = hitEntries[i];
		if(entry.baseAddress == lastBaseAddress) continue;
		if(!(maxEntries--)) break;
		char * funcName;
		if(!entry.funcName || !gbstrlen(entry.funcName))
			funcName = "Unknown";
		else funcName = entry.funcName;
		sb->safePrintf("<tr><td valign=\"top\">%s</td>"
			       "<td valign=\"top\">%i</td>"
			       "<td valign=\"top\">%i</td>"
			       "<td valign=\"top\">%#.8x</td>",
				funcName,
				entry.numHitsPerFunc,
				entry.missedQuickPollsPerFunc,
				entry.baseAddress);
		lastBaseAddress = entry.baseAddress;
		sb->safePrintf("<td><table>");
		sb->safePrintf("<tr><td>%i</td></tr>", entry.numHits);
		if(!realTimeShowAll) {
			sb->safePrintf("</table><td><table>");
			sb->safePrintf("<tr><td>%#.8x</td></tr>",entry.address);
			sb->safePrintf("<tr><td>...</td></tr>");
			sb->safePrintf("</table><td><table>");
			sb->safePrintf("<tr><td>%i</td></tr>",
				entry.missedQuickPolls);
			sb->safePrintf("<tr><td>...</td></tr>");
			sb->safePrintf("</table></td></td></tr>");
			continue;
		}
		for(uint32_t j = i + 1; j < index; ++j) {
			const HitEntry &entry = hitEntries[j];
			if(entry.baseAddress != lastBaseAddress) break;
			sb->safePrintf("<tr><td>%i</td></tr>",
				entry.numHits);
		}
		sb->safePrintf("</table><td><table>");
		sb->safePrintf("<tr><td>%#.8x</td></tr>", entry.address);
		for(uint32_t j = i + 1; j < index; ++j) {
			const HitEntry &entry = hitEntries[j];
			if(entry.baseAddress != lastBaseAddress) break;
			sb->safePrintf("<tr><td>%#.8x</td></tr>",
				entry.address);
		}
		sb->safePrintf("</table><td><table>");
		sb->safePrintf("<tr><td>%i</td></tr>",
				entry.missedQuickPolls);
		for(++i; i < index; ++i) {
			const HitEntry &entry = hitEntries[i];
			if(entry.baseAddress != lastBaseAddress) {
				sb->safePrintf("</table></td>"
					       "</td></tr>");
				goto top;
			}
			sb->safePrintf("<tr><td>%i</td></tr>",
					entry.missedQuickPolls);
		}
	}
	sb->safePrintf("</table></td></td></tr></table><br><br>\n");
	sb->safePrintf("<pre>");
	m_rootFrame->dump(sb);
	sb->safePrintf("</pre>");
	g_profiler.startRealTimeProfiler();	
	return true;
		*/
}

void
Profiler::cleanup() {
	m_rootFrame = 0;
}

FrameTrace *
Profiler::getNewFrameTrace(const uint32_t addr) {
	if(m_numUsedFrameTraces >= MAX_FRAME_TRACES) {
		log("profiler: Real time profiler ran out of static memory");
		return NULL;
	}
	return m_frameTraces[m_numUsedFrameTraces++].set(addr);
}

#endif
