#include "ArchiveParser.h"

#include "gb-include.h"
#include "HttpMime.h"

static int64_t parseInt64(const char *s, int32_t len) {
	if (!s || len <= 0) return 0;
	char tmp[64];
	if (len > (int32_t)sizeof(tmp) - 1) len = sizeof(tmp) - 1;
	gbmemcpy(tmp, s, len);
	tmp[len] = '\0';
	return atoll(tmp);
}

static int64_t parseTime(const char *s, int32_t len) {
	if (!s || len <= 0) return 0;
	char tmp[128];
	if (len > (int32_t)sizeof(tmp) - 1) len = sizeof(tmp) - 1;
	gbmemcpy(tmp, s, len);
	tmp[len] = '\0';
	return atotime(tmp);
}

static const char *findHeaderEnd(const char *buf, int64_t len, int32_t *delimLen) {
	if (delimLen) *delimLen = 0;
	if (!buf || len < 4) return NULL;
	for (int64_t i = 0; i + 3 < len; i++) {
		if (buf[i] == '\r' && buf[i + 1] == '\n' &&
		    buf[i + 2] == '\r' && buf[i + 3] == '\n') {
			if (delimLen) *delimLen = 4;
			return buf + i;
		}
	}
	for (int64_t i = 0; i + 1 < len; i++) {
		if (buf[i] == '\n' && buf[i + 1] == '\n') {
			if (delimLen) *delimLen = 2;
			return buf + i;
		}
	}
	return NULL;
}

static bool headerKeyMatches(const char *line, int32_t lineLen, const char *key) {
	int32_t keyLen = (int32_t)strlen(key);
	if (lineLen != keyLen) return false;
	for (int32_t i = 0; i < keyLen; i++) {
		if (to_lower_a(line[i]) != to_lower_a(key[i])) return false;
	}
	return true;
}

static bool getHeaderValue(const char *start,
			   const char *end,
			   const char *key,
			   const char **valueStart,
			   int32_t *valueLen) {
	if (valueStart) *valueStart = NULL;
	if (valueLen) *valueLen = 0;
	if (!start || !end || start >= end) return false;

	const char *line = start;
	while (line < end) {
		const char *lineEnd = (const char *)memchr(line, '\n', end - line);
		if (!lineEnd) lineEnd = end;

		const char *colon = (const char *)memchr(line, ':', lineEnd - line);
		if (colon) {
			int32_t nameLen = colon - line;
			if (headerKeyMatches(line, nameLen, key)) {
				const char *val = colon + 1;
				while (val < lineEnd && is_wspace_a(*val)) val++;
				const char *valEnd = lineEnd;
				if (valEnd > line && valEnd[-1] == '\r') valEnd--;
				while (valEnd > val && is_wspace_a(valEnd[-1])) valEnd--;
				if (valueStart) *valueStart = val;
				if (valueLen) *valueLen = valEnd - val;
				return true;
			}
		}

		line = lineEnd + 1;
	}
	return false;
}

static bool startsWithNoCase(const char *s, int32_t len, const char *prefix) {
	int32_t plen = (int32_t)strlen(prefix);
	if (len < plen) return false;
	for (int32_t i = 0; i < plen; i++) {
		if (to_lower_a(s[i]) != to_lower_a(prefix[i])) return false;
	}
	return true;
}

ArchiveParser::ArchiveParser()
	: m_type(ARCHIVE_WARC),
	  m_lastError(0),
	  m_lastErrorMsg(NULL) {
	m_record.reset();
}

bool ArchiveParser::init(ArchiveType type) {
	m_type = type;
	m_record.reset();
	m_lastError = 0;
	m_lastErrorMsg = NULL;
	return true;
}

ParseResult ArchiveParser::parseNext(const char *buf, int64_t bufLen, int64_t *consumed) {
	if (consumed) *consumed = 0;
	m_record.reset();

	if (!buf || bufLen <= 0) return PARSE_NEED_MORE;

	if (m_type == ARCHIVE_WARC) {
		const char *whp = buf;
		const char *end = buf + bufLen;
		for (; whp + 5 <= end; whp++) {
			if (whp[0] == 'W' &&
			    whp[1] == 'A' &&
			    whp[2] == 'R' &&
			    whp[3] == 'C' &&
			    whp[4] == '/') {
				break;
			}
		}
		if (whp + 5 > end) return PARSE_NEED_MORE;

		int32_t delimLen = 0;
		const char *headerEnd = findHeaderEnd(whp, end - whp, &delimLen);
		if (!headerEnd) return PARSE_NEED_MORE;

		const char *headerStart = whp;
		const char *headerStop = headerEnd;
		const char *contentStart = headerEnd + delimLen;

		const char *lenVal = NULL;
		int32_t lenValLen = 0;
		if (!getHeaderValue(headerStart, headerStop, "Content-Length", &lenVal, &lenValLen)) {
			m_lastError = 2;
			m_lastErrorMsg = "WARC Content-Length missing";
			return PARSE_ERROR;
		}

		int64_t contentLen = parseInt64(lenVal, lenValLen);
		if (contentLen < 0) {
			m_lastError = 3;
			m_lastErrorMsg = "WARC Content-Length invalid";
			return PARSE_ERROR;
		}

		int64_t recSize = (contentStart + contentLen) - headerStart;
		if (recSize <= 0) {
			m_lastError = 4;
			m_lastErrorMsg = "WARC record size invalid";
			return PARSE_ERROR;
		}

		if (contentStart + contentLen > end) return PARSE_NEED_MORE;

		int64_t skipped = headerStart - buf;
		if (skipped < 0) skipped = 0;

		const char *typeVal = NULL;
		int32_t typeValLen = 0;
		if (!getHeaderValue(headerStart, headerStop, "WARC-Type", &typeVal, &typeValLen)) {
			if (consumed) *consumed = recSize;
			return PARSE_SKIP_RECORD;
		}
		if (!startsWithNoCase(typeVal, typeValLen, "response")) {
			if (consumed) *consumed = recSize;
			return PARSE_SKIP_RECORD;
		}

		const char *conVal = NULL;
		int32_t conValLen = 0;
		if (!getHeaderValue(headerStart, headerStop, "Content-Type", &conVal, &conValLen)) {
			if (consumed) *consumed = recSize;
			return PARSE_SKIP_RECORD;
		}

		if (!(startsWithNoCase(conVal, conValLen, "application/http; msgtype=response") ||
		      startsWithNoCase(conVal, conValLen, "application/http;msgtype=response"))) {
			if (consumed) *consumed = recSize;
			return PARSE_SKIP_RECORD;
		}

		const char *urlVal = NULL;
		int32_t urlValLen = 0;
		if (!getHeaderValue(headerStart, headerStop, "WARC-Target-URI", &urlVal, &urlValLen)) {
			if (consumed) *consumed = recSize;
			return PARSE_SKIP_RECORD;
		}

		const char *dateVal = NULL;
		int32_t dateValLen = 0;
		getHeaderValue(headerStart, headerStop, "WARC-Date", &dateVal, &dateValLen);

		const char *ipVal = NULL;
		int32_t ipValLen = 0;
		getHeaderValue(headerStart, headerStop, "WARC-IP-Address", &ipVal, &ipValLen);

		m_record.url = const_cast<char *>(urlVal);
		m_record.urlLen = urlValLen;
		m_record.payload = const_cast<char *>(contentStart);
		m_record.payloadLen = contentLen;
		m_record.captureTime = parseTime(dateVal, dateValLen);
		m_record.ip = (ipVal && ipValLen > 0) ? atoip((char *)ipVal, ipValLen) : 0;
		m_record.contentType = CT_UNKNOWN;
		m_record.hasHttpResponse = true;

		if (consumed) *consumed = skipped + recSize;
		return PARSE_OK;
	}

	// ARCHIVE_ARC
	{
		const char *end = buf + bufLen;
		const char *whp = buf;
		for (; whp + 8 <= end; whp++) {
			if (whp[0] != '\n') continue;
			if (strncmp(whp + 1, "http://", 7) == 0) break;
			if (strncmp(whp + 1, "https://", 8) == 0) break;
		}
		if (whp + 8 > end) return PARSE_NEED_MORE;

		const char *lineStart = whp + 1;
		const char *lineEnd = (const char *)memchr(lineStart, '\n', end - lineStart);
		if (!lineEnd) return PARSE_NEED_MORE;

		const char *p = lineStart;
		const char *url = p;
		while (p < lineEnd && *p != ' ') p++;
		if (p >= lineEnd) {
			m_lastError = 6;
			m_lastErrorMsg = "ARC header missing url token";
			return PARSE_ERROR;
		}
		int32_t urlLen = p - url;
		while (p < lineEnd && *p == ' ') p++;

		const char *ipStr = p;
		while (p < lineEnd && *p != ' ') p++;
		if (p >= lineEnd) {
			m_lastError = 7;
			m_lastErrorMsg = "ARC header missing ip token";
			return PARSE_ERROR;
		}
		int32_t ipLen = p - ipStr;
		while (p < lineEnd && *p == ' ') p++;

		const char *timeStr = p;
		while (p < lineEnd && *p != ' ') p++;
		if (p >= lineEnd) {
			m_lastError = 8;
			m_lastErrorMsg = "ARC header missing time token";
			return PARSE_ERROR;
		}
		int32_t timeLen = p - timeStr;
		while (p < lineEnd && *p == ' ') p++;

		const char *ctStr = p;
		while (p < lineEnd && *p != ' ') p++;
		if (p >= lineEnd) {
			m_lastError = 9;
			m_lastErrorMsg = "ARC header missing content-type token";
			return PARSE_ERROR;
		}
		int32_t ctLen = p - ctStr;
		while (p < lineEnd && *p == ' ') p++;

		const char *lenStr = p;
		int32_t lenLen = lineEnd - lenStr;
		if (lenLen <= 0) {
			m_lastError = 10;
			m_lastErrorMsg = "ARC header missing content-length token";
			return PARSE_ERROR;
		}
		if (lenStr[lenLen - 1] == '\r') lenLen--;

		const char *payloadStart = lineEnd + 1;
		int64_t payloadLen = parseInt64(lenStr, lenLen);
		int64_t recSize = (payloadStart + payloadLen) - whp;
		if (recSize <= 0) {
			m_lastError = 11;
			m_lastErrorMsg = "ARC record size invalid";
			return PARSE_ERROR;
		}

		if (payloadStart + payloadLen > end) return PARSE_NEED_MORE;

		int64_t skipped = whp - buf;
		if (skipped < 0) skipped = 0;

		char tmp[128];
		if (ctLen > (int32_t)sizeof(tmp) - 1) ctLen = sizeof(tmp) - 1;
		gbmemcpy(tmp, ctStr, ctLen);
		tmp[ctLen] = '\0';

		m_record.url = const_cast<char *>(url);
		m_record.urlLen = urlLen;
		m_record.payload = const_cast<char *>(payloadStart);
		m_record.payloadLen = payloadLen;
		m_record.captureTime = parseTime(timeStr, timeLen);
		m_record.ip = (ipLen > 0) ? atoip((char *)ipStr, ipLen) : 0;
		m_record.contentType = getContentTypeFromStr(tmp);
		m_record.hasHttpResponse = true;

		if (consumed) *consumed = skipped + recSize;
		return PARSE_OK;
	}
}
