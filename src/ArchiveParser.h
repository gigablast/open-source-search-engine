#ifndef GB_ARCHIVEPARSER_H
#define GB_ARCHIVEPARSER_H

#include <stdint.h>
#include <stddef.h>

enum ArchiveType {
	ARCHIVE_WARC = 0,
	ARCHIVE_ARC  = 1
};

enum ParseResult {
	PARSE_OK = 0,
	PARSE_NEED_MORE = 1,
	PARSE_SKIP_RECORD = 2,
	PARSE_ERROR = 3
};

struct ArchiveRecord {
	char *url;
	int32_t urlLen;
	char *payload;
	int64_t payloadLen;
	int64_t captureTime;
	int32_t ip;
	int32_t contentType;
	bool hasHttpResponse;

	void reset() {
		url = NULL;
		urlLen = 0;
		payload = NULL;
		payloadLen = 0;
		captureTime = 0;
		ip = 0;
		contentType = 0;
		hasHttpResponse = false;
	}
};

class ArchiveParser {
public:
	ArchiveParser();

	bool init(ArchiveType type);
	ParseResult parseNext(const char *buf, int64_t bufLen, int64_t *consumed);
	const ArchiveRecord &getRecord() const { return m_record; }
	int32_t lastError() const { return m_lastError; }
	const char *lastErrorMsg() const { return m_lastErrorMsg; }

private:
	ArchiveType m_type;
	ArchiveRecord m_record;
	int32_t m_lastError;
	const char *m_lastErrorMsg;
};

#endif
