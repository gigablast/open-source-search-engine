
// See LanguagePages.h for docs.

#include "gb-include.h"
#include "Mem.h"
#include "LanguagePages.h"

LanguagePages g_languagePages;

int uint8strlen(uint8_t *str) {
	int len = 0;
	while(*str++) len++;
	return(len);
}

LanguagePages::LanguagePages() {
	m_loading = false;
	memset(m_languagePages, 0, sizeof(m_languagePages));
	memset(m_languageHeaders, 0, sizeof(m_languagePages));
	memset(m_languageFooters, 0, sizeof(m_languagePages));
	memset(m_languageAllocated, 0, sizeof(m_languageAllocated));
	memset(m_languageAllocatedHeaders, 0, sizeof(m_languageAllocatedHeaders));
	memset(m_languageAllocatedFooters, 0, sizeof(m_languageAllocatedFooters));
	memset(m_PageSize, 0, sizeof(m_PageSize));
	memset(m_HeaderSize, 0, sizeof(m_HeaderSize));
	memset(m_FooterSize, 0, sizeof(m_FooterSize));
}

LanguagePages::~LanguagePages() {
	int x;
	for(x = 0; x < MAX_LANGUAGES; x++) {
		if(m_languageAllocated[x])
			mfree(m_languagePages[x],
					m_languageAllocated[x],
					"langPage");
		if(m_languageAllocatedHeaders[x])
			mfree(m_languageHeaders[x],
					m_languageAllocatedHeaders[x],
					"langHeader");
		if(m_languageAllocatedFooters[x])
			mfree(m_languageFooters[x],
					m_languageAllocatedFooters[x],
					"langFooter");
	}
}

bool LanguagePages::setLanguagePage(uint8_t lang,
		uint8_t *pageText,
		bool handOver) {
	if(lang > MAX_LANGUAGES) return(false);
	if(m_languageAllocated[lang]) {
		mfree(m_languagePages[lang], m_languageAllocated[lang], "langPage");
		m_languageAllocated[lang] = 0;
	}
	if(!handOver) {
		m_languagePages[lang] =
			(uint8_t *)mmalloc(uint8strlen(pageText) + 1, "langPage");
		if(!m_languagePages[lang]) return(false);
		memset(m_languagePages[lang], 0, uint8strlen(pageText) + 1);
		gbmemcpy(m_languagePages[lang], pageText, uint8strlen(pageText));
	} else {
		m_languagePages[lang] = pageText;
	}
	m_languageAllocated[lang] = uint8strlen(pageText) + 1;
	return(true);
}

bool LanguagePages::setLanguageHeader(uint8_t lang,
		uint8_t *pageText,
		bool handOver) {
	if(lang > MAX_LANGUAGES) return(false);
	if(m_languageAllocatedHeaders[lang]) {
		mfree(m_languageHeaders[lang],
				m_languageAllocatedHeaders[lang],
				"langHeader");
		m_languageAllocatedHeaders[lang] = 0;
	}
	if(!handOver) {
		m_languageHeaders[lang] =
			(uint8_t *)mmalloc(uint8strlen(pageText) + 1, "langHeader");
		if(!m_languageHeaders[lang]) return(false);
		memset(m_languageHeaders[lang], 0, uint8strlen(pageText) + 1);
		gbmemcpy(m_languageHeaders[lang], pageText, uint8strlen(pageText));
	} else {
		m_languageHeaders[lang] = pageText;
	}
	m_languageAllocatedHeaders[lang] = uint8strlen(pageText) + 1;
	return(true);
}

bool LanguagePages::setLanguageFooter(uint8_t lang,
		uint8_t *pageText,
		bool handOver) {
	if(lang > MAX_LANGUAGES) return(false);
	if(m_languageAllocatedFooters[lang]) {
		mfree(m_languageFooters[lang],
				m_languageAllocatedFooters[lang],
				"langFooter");
		m_languageAllocatedFooters[lang] = 0;
	}
	if(!handOver) {
		m_languageFooters[lang] =
			(uint8_t *)mmalloc(uint8strlen(pageText) + 1, "langFooter");
		if(!m_languageFooters[lang]) return(false);
		memset(m_languageFooters[lang], 0, uint8strlen(pageText) + 1);
		gbmemcpy(m_languageFooters[lang], pageText, uint8strlen(pageText));
	} else {
		m_languageFooters[lang] = pageText;
	}
	m_languageAllocatedFooters[lang] = uint8strlen(pageText) + 1;
	return(true);
}

uint8_t *LanguagePages::getLanguagePage(uint8_t lang, int32_t *len) {
	if(lang > MAX_LANGUAGES) return(NULL);
	if(m_loading) return(NULL);
	if(len) *len = m_PageSize[lang];
	return m_languagePages[lang];
}

uint8_t *LanguagePages::getLanguageHeader(uint8_t lang, int32_t *len) {
	if(lang > MAX_LANGUAGES) return(NULL);
	if(m_loading) return(NULL);
	if(len) *len = m_HeaderSize[lang];
	return m_languageHeaders[lang];
}

uint8_t *LanguagePages::getLanguageFooter(uint8_t lang, int32_t *len) {
	if(lang >= MAX_LANGUAGES) return(NULL);
	if(m_loading) return(NULL);
	if(len) *len = m_FooterSize[lang];
	return m_languageFooters[lang];
}

void LanguagePages::reloadPages(void) {
	int fd;
	int x;
	struct stat s;
	char filename[2048];
	uint8_t buf[10240];
	m_loading = true;
	for(x = 0; x < MAX_LANGUAGES; x++) {

		// Load homepage, if there is one
		snprintf(filename, 2047, "%s/langPages/homepage_%d.template",
				g_hostdb.m_dir, x);
		if((fd = open(filename, O_RDONLY)) < 0) continue;
		memset(buf, 0, 10240);
		if(!fstat(fd, &s) &&
				s.st_size < 10240 &&
				read(fd, buf, s.st_size) == s.st_size) {
			log(LOG_INIT, "admin: Loading homepage for language %s\n",
					getLanguageString(x));
			setLanguagePage(x, buf, false);
			m_PageSize[x] = s.st_size;
		}
		close(fd);

		// Load header, if there is one
		snprintf(filename, 2047, "%s/langPages/header_%d.template",
				g_hostdb.m_dir, x);
		if((fd = open(filename, O_RDONLY)) < 0) continue;
		memset(buf, 0, 10240);
		if(!fstat(fd, &s) &&
				s.st_size < 10240 &&
				read(fd, buf, s.st_size) == s.st_size) {
			log(LOG_INIT, "admin: Loading header for language %s\n",
					getLanguageString(x));
			setLanguageHeader(x, buf, false);
			m_HeaderSize[x] = s.st_size;
		}
		close(fd);

		// Load footer, if there is one
		snprintf(filename, 2047, "%s/langPages/footer_%d.template",
				g_hostdb.m_dir, x);
		if((fd = open(filename, O_RDONLY)) < 0) continue;
		memset(buf, 0, 10240);
		if(!fstat(fd, &s) &&
				s.st_size < 10240 &&
				read(fd, buf, s.st_size) == s.st_size) {
			log(LOG_INIT, "admin: Loading footer for language %s\n",
					getLanguageString(x));
			setLanguageFooter(x, buf, false);
			m_FooterSize[x] = s.st_size;
		}
		close(fd);
	}
	m_loading = false;
}

