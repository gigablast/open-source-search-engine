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
unsigned long *indexTable;
unsigned long *keyTable;
unsigned long long *valueTableUll;
float *valueTableF;
//HashTableT<uint32_t, uint64_t> realTimeProfilerData;
#include "HashTableX.h"
HashTableX realTimeProfilerData;
uint32_t lastQuickPollAddress = 0;
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
}

Profiler::~Profiler() {//reset();
	reset();
}

bool Profiler::reset(){
	m_fn.reset();
	m_lastQPUsed = 0;
	for (long i=0;i<11;i++)
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
	return true;
}

bool Profiler::init() {
	m_lastQPUsed = 0;
	realTimeProfilerData.set(4,8,0,NULL,0,false,0,"rtprof");
        m_quickpolls.set(4,4,0,NULL,0,false,0,"qckpllcnt");
	for (long i=0;i<11;i++)
		//m_fnTmp[i].set(256);
		if ( ! m_fnTmp[i].set(4,sizeof(FnInfo),256,NULL,0,false,0,
				      "fntmp"))
			return false;
	if ( ! m_activeFns.set(4,4,256,NULL,0,false,0,"activefns") )
		return false;
	return m_fn.set(4,sizeof(FnInfo),256,NULL,0,false,0,"fntbl");
}


// This reads the symbol table of gb executable (thought to be the file gb in 
// the working directory) into a hashtable.
// The gb executable file is in the ELF format, and the code here resembles 
// readelf function in binutils from gnu.org. gb is 32-bits.
bool Profiler:: readSymbolTable(){
	long long start=gettimeofdayInMillisecondsLocal();
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
	
	long long end=gettimeofdayInMillisecondsLocal();
	log(LOG_INIT,"admin: Took %lli milliseconds to build symbol table",
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
	//Found that bfd_vma type is unsigned long	
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

unsigned long Profiler::getByte (unsigned char * field,int size){
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
		return  ((unsigned long) (field [0]))
			|    (((unsigned long) (field [1])) << 8)
			|    (((unsigned long) (field [2])) << 16)
			|    (((unsigned long) (field [3])) << 24);
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
			if (((long)psym->st_size)>0)
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
	  
		log(LOG_INIT,"admin: Symbol table '%s' contains %lu entries",
		    m_stringTable+section->sh_name,
		    (unsigned long) (section->sh_size / section->sh_entsize));
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
				    "of %s at %lx", "string table",
				    string_sec->sh_offset);
				return 0;
			}
			strtab = (char *) mmalloc (string_sec->sh_size,
						   "ProfilerG");
			if (strtab == NULL){
				log(LOG_INIT,"admin: Out of memory allocating "
				    "%ld bytes for %s", string_sec->sh_size,
				    "string table");
			}
			if (fread ( strtab, string_sec->sh_size, 1, 
				    m_file) != 1 ){
				log(LOG_INIT,"admin: Unable to read in %ld "
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
			if (((long)psym->st_size)>0){
				//				FnInfo *fnInfo;
				long key = psym->st_value;
				long slot=m_fn.getSlot(&key);
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
					// "same address space %li",
					// (long)psym->st_value);
				}
				else{
					FnInfo fnInfoTmp;
					strncpy(fnInfoTmp.m_fnName,
						strtab+psym->st_name,255);
					
					char* end = strnstr(fnInfoTmp.m_fnName,
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
					unsigned long address=(long)psym->st_value;
					//log(LOG_WARN,"Profiler: Adding fninfo name=%s, key=%li",
					// fnInfo->m_fnName,address);
					long key = (long)address;
					m_fn.addKey(&key,&fnInfoTmp);
					m_addressMap[m_lastAddressMapIndex++] = address;
				}
			}
			/*log(LOG_WARN,"%6d\t %8.8lx\t   %5ld\t   %s", 
			    si,(unsigned long)psym->st_value,
			    (long)psym->st_size,strtab + psym->st_name);*/
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
					       unsigned long offset,
					       unsigned long number){	
	Elf32_External_Sym* esyms;
	Elf_Internal_Sym *isyms;
	Elf_Internal_Sym *psym;
	unsigned int j;
	
	//	GET_DATA_ALLOC (offset, number * sizeof (Elf32_External_Sym),
	//  esyms, Elf32_External_Sym *, "symbols");
	
	if (fseek(file, offset, SEEK_SET)){
		log(LOG_INIT,"admin: Unable to seek to start of %s at %lx", "symbols", offset);
		return 0;
	}
	esyms = (Elf32_External_Sym *) 
		mmalloc (number * sizeof (Elf32_External_Sym),"ProfilerE");
	if (esyms==NULL){
		log(LOG_INIT,"admin: Out of memory allocating %ld bytes for %s",
		    number *sizeof (Elf32_External_Sym),"Symbols");
		return 0;
	}

	if (fread (esyms,number * sizeof (Elf32_External_Sym), 1, file) != 1){ 
		log(LOG_INIT,"admin: Unable to read in %ld bytes of %s", 
		    number * sizeof (Elf32_External_Sym), "symbols");
		mfree (esyms,number * sizeof (Elf32_External_Sym),"ProfilerE");
		esyms = NULL;
		return 0;
	}
	long need = number * sizeof (Elf_Internal_Sym);
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
			    "at %lx\n","string table",section->sh_offset);
			return 0;
		}
		m_stringTableSize=section->sh_size;
		m_stringTable = (char *) mmalloc (m_stringTableSize,
						  "ProfilerB");
		if (m_stringTable == NULL){
			log(LOG_INIT,"admin: Out of memory allocating %ld "
			    "bytes for %s\n", section->sh_size,"string table");
			return 0;
		}
		if (fread (m_stringTable, section->sh_size, 1, m_file) != 1){
			log(LOG_INIT,"admin: Unable to read in %ld bytes of "
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
		log(LOG_INIT,"admin: Unable to seek to start of %s at %lx\n",
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


bool Profiler::startTimer(long address, const char* caller) {
	// disable - we do interrupt based profiling now
	return true;
	if(g_inSigHandler) return 1;
	long slot = m_fn.getSlot(&address);
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
bool Profiler::pause(const char* caller, long lineno, long took) {
	lastQuickPollTime = gettimeofdayInMicroseconds(); 
	unsigned long long nowLocal = lastQuickPollTime / 1000;
	void *trace[3];
	backtrace(trace, 3);
	const void *stackPtr = trace[2];
	lastQuickPollAddress = (uint32_t)stackPtr; 
	for(long i = 0; i < m_activeFns.getNumSlots(); i++) {
		//if(m_activeFns.getKey(i) == 0) continue;
		if ( m_activeFns.isEmpty(i) ) continue;
		FnInfo* fnInfo = *(FnInfo **)m_activeFns.getValueFromSlot(i);
		unsigned long long blockedTime = nowLocal - 
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
// 	   log(LOG_WARN, "admin qp %s--%li took %li",
// 	       caller, lineno, took);
	long qpkey = (long)caller + lineno;
	long slot = m_quickpolls.getSlot(&qpkey);
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
	unsigned long long nowLocal = gettimeofdayInMillisecondsLocal();
 	for(long i = 0; i < m_activeFns.getNumSlots(); i++) {
		//if(m_activeFns.getKey(i) == 0) continue;
		if ( m_activeFns.isEmpty(i) ) continue;
		FnInfo* fnInfo = *(FnInfo **)m_activeFns.getValueFromSlot(i);
		fnInfo->m_lastPauseTime = nowLocal;
 	}
	return true;
}

bool Profiler::endTimer(long address,
			const char *caller,
			bool isThread ) {
	// disable - we do interrupt based profiling now
	if(g_inSigHandler) return 1;
	FnInfo *fnInfo;
	long slot = m_activeFns.getSlot(&address);
	if (slot < 0 ) {
		//log(LOG_WARN,"Profiler: got a non added function at 
		// address %li",address);
		// This happens because at closing the profiler is still on
		// after destructor has been called. Not displaying address
		// because is is of no use
		//		{ char *xx = NULL; *xx = 0; }
		//		return false;
		return true;
	}
	fnInfo=*(FnInfo **)m_activeFns.getValueFromSlot(slot);
	if(--fnInfo->m_inFunction > 0) return true;

	unsigned long long nowLocal = gettimeofdayInMillisecondsLocal();
	//unsigned long long now = gettimeofdayInMilliseconds();
	unsigned long long timeTaken = nowLocal - fnInfo->m_startTimeLocal;

	unsigned long long blockedTime = nowLocal - fnInfo->m_lastPauseTime ;
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


	if (timeTaken > (unsigned long)g_conf.m_minProfThreshold) {
		if(g_conf.m_sequentialProfiling)
			log(LOG_TIMING, "admin: %lli ms in %s from %s", 
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

	for (long i=0;i<11;i++){
		//if we find a hashtable is less than 1 second old
		unsigned long long diffTime=nowLocal-m_fnTime[i];
		if((diffTime<1000)&&(m_fnTime[i]!=0)){
			//Add this function. Don't add the function name,
			//shall get that from m_fn
			//log(LOG_WARN,"Profiler: adding funtion to existing "
			//"hashtable i=%li,now=%lli,"
			// "m_fnTime=%lli, diffTime=%lli",i,now,
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
	for (long i=0;i<11;i++){
		unsigned long long diffTime=nowLocal-m_fnTime[i];
		if((diffTime>=10000) || (m_fnTime[i]==0)){
			/*log(LOG_WARN,"Profiler: m_fntime=%lli,i=%li,now=%lli,diffTime=%lli",
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

bool Profiler::printInfo(SafeBuf *sb,char *username, //long user, 
                         char *pwd, char *coll, 
			 int sorts,int sort10, int qpreset,
			 int profilerreset) {
	// sort by max blocked time by default
	if ( sorts == 0 ) sorts = 8;

	long slot;
	unsigned long key(0);
	long numSlots = m_fn.getNumSlots();
	long numSlotsUsed = m_fn.getNumSlotsUsed();
	FnInfo *fnInfo;

	if ( profilerreset ) {
		for ( long i = 0; i < m_fn.getNumSlots(); i++ ){
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


	sb->safePrintf(  "<center>\n<table border=1 cellpadding=4 "
			 "width=100%% bgcolor=#%s>\n"
			 "<tr><td colspan=9 bgcolor=#%s>"
			 "<center><b>Profiler "//- Since Startup</b></center>"
			 "<a href=\"/admin/profiler?c=%s"//"
			 "&profilerreset=1\">"
			 "(reset)</a></b></center>"
			 "</td></tr>\n",LIGHT_BLUE,DARK_BLUE,
			 coll);

       	sb->safePrintf("<tr><td><b>Address</b></td><td><b>Function</b></td>");
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

	indexTable=(unsigned long*) 
		mcalloc(numSlotsUsed*sizeof(unsigned long),"ProfilerW");
	keyTable=(unsigned long*) mcalloc
		(numSlotsUsed*sizeof(unsigned long),"ProfilerX");
	if(sorts==5 ||sort10==5)
		valueTableF=(float*) 
			mcalloc(numSlotsUsed*sizeof(float),"ProfilerY");
	else
		valueTableUll=(unsigned long long*) 
			mcalloc(numSlotsUsed*sizeof(unsigned long long),
				"ProfilerY");
	long numFnsCalled=0;
	for (long i=0;i<numSlots;i++){
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
		gbqsort(indexTable,numFnsCalled,sizeof(unsigned long),
		      decend_cmpF);
	else
		gbqsort(indexTable,numFnsCalled,sizeof(unsigned long),
		      decend_cmpUll);

	//Now print the sorted values
	for (long i=0;i<numFnsCalled;i++){
		slot=m_fn.getSlot(&keyTable[indexTable[i]]);
		fnInfo=(FnInfo *)m_fn.getValueFromSlot(slot);
		//Don't print functions that have not been called
		sb->safePrintf("<tr><td>%lx</td><td>%s</td><td>%li</td><td>%li</td>"
			       "<td>%.4f</td><td>%li</td><td>%li</td><td>%li</td>"
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
	sb->safePrintf(  "<center>\n<table border=1 cellpadding=4 "
			 "width=100%% bgcolor=#%s>\n"
			 "<tr><td colspan=8 bgcolor=#%s>"
			 "<center><b>Profiler - Last 10 seconds</b></center>"
			 "</td></tr>\n",LIGHT_BLUE,DARK_BLUE);
       	sb->safePrintf("<tr><td><b>Address</b></td><td><b>Function</b></td>");
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
	unsigned long long now=gettimeofdayInMillisecondsLocal();
	long numFnsCalled10=0;;
	for(long i=0;i<numFnsCalled;i++){
		unsigned long long timesCalled=0;
		unsigned long long totalTimeTaken=0;
		unsigned long long maxTimeTaken=0;
		unsigned long long numCalledFromThread=0;
		//If hashtable is less than 10 secs old, use it
		for(long j=0;j<11;j++){
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
		gbqsort(indexTable,numFnsCalled10,sizeof(unsigned long),
		      decend_cmpF);
	else
		gbqsort(indexTable,numFnsCalled10,sizeof(unsigned long),
		      decend_cmpUll);

	for(long i=0;i<numFnsCalled10;i++){
		unsigned long long timesCalled=0;		
		unsigned long long totalTimeTaken=0;
		unsigned long long maxTimeTaken=0;
		unsigned long long numCalledFromThread=0;
		//If hashtable is less than 10 secs old, continue
		for(long j=0;j<11;j++){
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
		sb->safePrintf("<tr><td>%lx</td><td>%s</td><td>%lli</td>"
			       "<td>%lli</td>"
			       "<td>%.4f</td><td>%lli</td><td>%lli</td></tr>",
			       keyTable[indexTable[i]],
			       fnInfo->m_fnName,
			       timesCalled,
			       totalTimeTaken,
			       ((float)totalTimeTaken)/((float)timesCalled),
			       maxTimeTaken,
			       numCalledFromThread);
	}
	sb->safePrintf("</table><br><br>");

	
	
	mfree(indexTable,numSlotsUsed*sizeof(unsigned long),"ProfilerX");
	mfree(keyTable,numSlotsUsed*sizeof(unsigned long),"ProfilerX");
	if (sorts==5 || sort10==5)
		mfree(valueTableF,
		      numSlotsUsed*sizeof(float),
		      "ProfilerY");
	else
		mfree(valueTableUll,
		      numSlotsUsed*sizeof(unsigned long long),
		      "ProfilerY");



	if(qpreset) {
		m_quickpolls.clear();
		m_lastQPUsed = 0;
	}

	numSlots = m_quickpolls.getNumSlots();
	numSlotsUsed = m_quickpolls.getNumSlotsUsed();
	sb->safePrintf("<center>\n<table border=1 cellpadding=4 "
		       "width=100%% bgcolor=#%s>\n"
		       "<tr><td colspan=5 bgcolor=#%s>"
		       "<center><b>Triggered Quickpolls "
		       "<a href=\"/admin/profiler?c=%s"
		       "&qpreset=1\">"
		       "(reset)</a></b></center>"
		       "</td></tr>\n",LIGHT_BLUE,DARK_BLUE,
		       coll);

	sb->safePrintf("<tr><td><b>Between Functions</b></td>"
		       "<td><b>max blocked(msec)</b></td>"
		       "<td><b>avg time(msec)</b></td>"
		       "<td><b>times triggered</b></td>"
		       "<td><b>total(msec)</b></td>"
		       "</tr>");

	if(numSlotsUsed == 0) {
		sb->safePrintf("</table>");
		return true;
	}

	valueTableUll = (unsigned long long*)
		mcalloc(numSlotsUsed * sizeof(unsigned long long),"ProfilerZ");
	if(!valueTableUll) {
		sb->safePrintf("</table>");
		return true;
	}

	indexTable = (unsigned long*)mcalloc(numSlotsUsed * 
					     sizeof(unsigned long),
					     "ProfilerZ");
	if(!indexTable) {
		mfree(indexTable,   
		      numSlotsUsed*sizeof(unsigned long),
		      "ProfilerZ");
		sb->safePrintf("</table>");
		return true;
	}

	keyTable = (unsigned long*)mcalloc(numSlotsUsed * 
					   sizeof(unsigned long),
					   "ProfilerZ");
	if(!keyTable) {
		mfree(indexTable,   
		      numSlotsUsed*sizeof(unsigned long),
		      "ProfilerZ");
		mfree(valueTableUll,
		      numSlotsUsed*sizeof(unsigned long long),
		      "ProfilerZ");
		sb->safePrintf("</table>");
		return true;
	}

	long j = 0;
	for (long i = 0; i < numSlots; i++) {
		//if((key = m_quickpolls.getKey(i)) == 0) continue;
		if ( m_quickpolls.isEmpty(i) ) continue;
		QuickPollInfo* q = *(QuickPollInfo **)m_quickpolls.getValueFromSlot(i);
		long took = q->m_maxTime;
		valueTableUll[j] = took;
		indexTable[j] = j; 
		keyTable[j] = i; 
		j++;
	}
	gbqsort(indexTable, j, sizeof(unsigned long), decend_cmpUll);
	
	for (long i = 0; i < numSlotsUsed; i++){
		long slot = keyTable[indexTable[i]];
		//key = m_quickpolls.getKey(slot);
		QuickPollInfo* q = *(QuickPollInfo **)m_quickpolls.getValueFromSlot(slot);
		sb->safePrintf("<tr><td>%s:%li<br>%s:%li</td>"
			       "<td>%li</td>"
			       "<td>%f</td>"
			       "<td>%li</td>"
			       "<td>%li</td>"
			       "</tr>",
			       q->m_caller,  q->m_lineno, q->m_last, 
			       q->m_lastlineno,q->m_maxTime,
			       (float)q->m_timeAcc / q->m_times,
			       q->m_times,
			       q->m_timeAcc);
	}
	sb->safePrintf("</table>");

	mfree(valueTableUll,numSlotsUsed*sizeof(unsigned long long),"ProfilerZ");
	mfree(indexTable,   numSlotsUsed*sizeof(unsigned long),"ProfilerZ");
	mfree(keyTable,     numSlotsUsed*sizeof(unsigned long),"ProfilerZ");
	return true;
}

//backwards so we get highest scores first.
static int decend_cmpUll ( const void *h1 , const void *h2 ) {
        unsigned long tmp1, tmp2;
        tmp1 = *(unsigned long *)h1;
	tmp2 = *(unsigned long *)h2;
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
        unsigned long tmp1, tmp2;
        tmp1 = *(unsigned long *)h1;
	tmp2 = *(unsigned long *)h2;
	if (valueTableF[tmp1]>valueTableF[tmp2]) {
		return -1;	
	}
        else if(valueTableF[tmp1]<valueTableF[tmp2]){
		return 1;
	}
	else return 0;
}


char* Profiler::getFnName(unsigned long address,long *nameLen){
	FnInfo *fnInfo;
	long slot=m_fn.getSlot(&address);
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
	//long  user     = g_pages.getUserType( s , r );
	char *username = g_users.getUsername(r);
	//char *pwd  = r->getString ("pwd");

	char *coll = r->getString ("c");
	long collLen;
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

	
	if (!g_conf.m_profilingEnabled)
		sb.safePrintf("<font color=#ff0000><b><centeR>"
			      "Sorry, this feature is temporarily disabled. "
			      "Enable it in MasterControls.</center></b></font>");
	else {
		if(g_profiler.m_realTimeProfilerRunning) {
			if(stopRt) g_profiler.stopRealTimeProfiler();
		} else if(startRt)   g_profiler.startRealTimeProfiler();
				
		g_profiler.printRealTimeInfo(&sb,
					     username,
					     NULL,
					     coll,
					     realTimeSortMode,
					     realTimeShowAll);
		g_profiler.printInfo(&sb,username,NULL,coll,sorts,sort10, qpreset,
				     profilerreset);
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
	for(uint32_t i = numFrames - 3; i > 1; --i) {
		uint32_t base =
			g_profiler.getFuncBaseAddr((uint32_t)trace[i]);
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
	uint32_t stackPtr = (uint32_t)trace[2];
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

void
Profiler::getStackFrame(int sig) {
	void *trace[32];
	uint32_t numFrames = backtrace(trace, 32);
	if(numFrames < 3) return;
	const void *stackPtr = trace[2];
	uint32_t baseAddress = g_profiler.getFuncBaseAddr((uint32_t)stackPtr);
	uint32_t *ptr;	
	FrameTrace *frame = updateRealTimeData(trace, numFrames, &ptr);
	if(baseAddress != g_profiler.m_lastDeltaAddress) {
		// This function is different from the last function we saw
		g_profiler.m_lastDeltaAddressTime =gettimeofdayInMilliseconds();
		g_profiler.m_lastDeltaAddress = baseAddress;
	}
	checkMissedQuickPoll( 	frame,
				(uint32_t)stackPtr,
				ptr);
}

void
Profiler::startRealTimeProfiler() {
	log(LOG_INIT, "admin:  MLT starting real time profiler");
	if(!m_frameTraces) {
		m_frameTraces = (FrameTrace *)mmalloc(
			sizeof(FrameTrace) * MAX_FRAME_TRACES, "FrameTraces");
		memset(m_frameTraces, 0, sizeof(FrameTrace) * MAX_FRAME_TRACES);
		m_numUsedFrameTraces = 0;
		m_rootFrame = &m_frameTraces[m_numUsedFrameTraces++];
	}
	struct itimerval value, ovalue;
	int which = ITIMER_REAL;
	//signal(SIGVTALRM, Profiler::getStackFrame);
	signal(SIGALRM, Profiler::getStackFrame);
	value.it_interval.tv_sec = 0;
	value.it_interval.tv_usec = 5000;
	value.it_value.tv_sec = 0;
	value.it_value.tv_usec = 5000;
	setitimer( which, &value, &ovalue );
	m_realTimeProfilerRunning = true;
}

void
Profiler::stopRealTimeProfiler(const bool keepData) {
	log(LOG_INIT, "admin:  MLT stopping real time profiler");
	struct itimerval value;
	int which = ITIMER_REAL;
	getitimer( which, &value );
	value.it_value.tv_sec = 0;
	value.it_value.tv_usec = 0;
	setitimer( which, &value, NULL );
	m_realTimeProfilerRunning = false;
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

bool
Profiler::printRealTimeInfo(SafeBuf *sb,
			    //long user,
			    char *username,
			    char *pwd,
			    char *coll,
			    int realTimeSortMode,
			    int realTimeShowAll) {
	if(!m_realTimeProfilerRunning) {
		sb->safePrintf("<table border=1 cellpadding=4 bgcolor=#%s "
			       "width=100%%\n>",
			LIGHT_BLUE);
		sb->safePrintf("<tr><td colspan=7 bgcolor=#%s>"
			 "<center><b>Real Time Profiler "
			 "<a href=\"/admin/profiler?c=%s"
			 "&rtstart=1\">"
			 "(Start)</a></b></center>"
			       "</td></tr>\n",DARK_BLUE,coll);
		sb->safePrintf("</table><br><br>\n");
		return true;
	}
	stopRealTimeProfiler(true);
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
		sb->safePrintf("<table border=1 cellpadding=4 bgcolor=#%s "
			       "width=100%%\n>",
			LIGHT_BLUE);
		sb->safePrintf("<tr><td colspan=7 bgcolor=#%s>"
			 "<center><b>Real Time Profiler started, refresh page "
			 "after some time."
			 "<a href=\"/admin/profiler?c=%s"
			 "&rtstop=1\">"
			 "(Stop)</a></b></center>"
			       "</td></tr>\n",DARK_BLUE,coll);
		sb->safePrintf("</table><br><br>\n");
		startRealTimeProfiler();
		return true;
	}
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
	sb->safePrintf("<table border=1 cellpadding=4 bgcolor=#%s "
		       "width=100%%>\n",
			LIGHT_BLUE);
	char *showMessage;
	int rtall;
	if(realTimeShowAll) {
		showMessage = "(show only 10)";
		rtall = 0;
	} else {
		showMessage = "(show all)";
		rtall = 1;
	}
	sb->safePrintf("<tr><td colspan=7 bgcolor=#%s>"
			 "<center><b>Real Time Profiler "
			 "<a href=\"/admin/profiler?c=%s"
			 "&rtall=%i\">%s</a>"
		       ,DARK_BLUE,coll,
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

