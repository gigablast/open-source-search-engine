#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "ArchiveParser.h"
#include "SafeBuf.h"
#include "HttpMime.h"
#include "Mem.h"

#include <string.h>

// ------------------------------------------
bool g_recoveryMode = false;
int g_inMemcpy = 0;
int32_t g_recoveryLevel = 0;

bool sendPageSEO(TcpSocket *s, HttpRequest *hr) {
	return false;
}
// ------------------------------------------

TEST_CASE("ArchiveParser parses a basic WARC response record", "[ArchiveParser][WARC]") {
	const char *payload =
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: text/html\r\n"
		"Content-Length: 5\r\n"
		"\r\n"
		"Hello";
	int64_t payloadLen = (int64_t)strlen(payload);

	char buf[1024];
	int32_t n = snprintf(buf, sizeof(buf),
			     "WARC/1.0\r\n"
			     "WARC-Type: response\r\n"
			     "WARC-Target-URI: http://example.com/\r\n"
			     "WARC-Date: 2023-09-21T07:37:11Z\r\n"
			     "WARC-IP-Address: 1.2.3.4\r\n"
			     "Content-Type: application/http; msgtype=response; charset=UTF-8\r\n"
			     "Content-Length: %" INT64 "\r\n"
			     "\r\n"
			     "%s",
			     payloadLen, payload);
	REQUIRE(n > 0);

	ArchiveParser parser;
	parser.init(ARCHIVE_WARC);
	int64_t consumed = 0;
	ParseResult pr = parser.parseNext(buf, n, &consumed);
	REQUIRE(pr == PARSE_OK);
	REQUIRE(consumed == n);

	const ArchiveRecord &rec = parser.getRecord();
	REQUIRE(rec.urlLen == (int32_t)strlen("http://example.com/"));
	REQUIRE(strncmp(rec.url, "http://example.com/", rec.urlLen) == 0);
	REQUIRE(rec.payloadLen == payloadLen);
	REQUIRE(strncmp(rec.payload, payload, payloadLen) == 0);
	REQUIRE(rec.hasHttpResponse == true);
}

TEST_CASE("ArchiveParser accepts mixed-case WARC headers and LF delimiter", "[ArchiveParser][WARC]") {
	const char *payload =
		"HTTP/1.1 200 OK\n"
		"Content-Type: text/plain\n"
		"Content-Length: 2\n"
		"\n"
		"OK";
	int64_t payloadLen = (int64_t)strlen(payload);

	char buf[1024];
	int32_t n = snprintf(buf, sizeof(buf),
			     "WARC/1.1\n"
			     "warc-type: response\n"
			     "warc-target-uri: http://example.com/mixed\n"
			     "content-type: application/http; msgtype=response; charset=UTF-8\n"
			     "content-length: %" INT64 "\n"
			     "\n"
			     "%s",
			     payloadLen, payload);
	REQUIRE(n > 0);

	ArchiveParser parser;
	parser.init(ARCHIVE_WARC);
	int64_t consumed = 0;
	ParseResult pr = parser.parseNext(buf, n, &consumed);
	REQUIRE(pr == PARSE_OK);
	REQUIRE(consumed == n);

	const ArchiveRecord &rec = parser.getRecord();
	REQUIRE(rec.urlLen == (int32_t)strlen("http://example.com/mixed"));
	REQUIRE(strncmp(rec.url, "http://example.com/mixed", rec.urlLen) == 0);
	REQUIRE(rec.payloadLen == payloadLen);
}

TEST_CASE("ArchiveParser skips non-response WARC records", "[ArchiveParser][WARC]") {
	const char *payload = "HTTP/1.1 200 OK\r\n\r\nX";
	int64_t payloadLen = (int64_t)strlen(payload);

	char buf[512];
	int32_t n = snprintf(buf, sizeof(buf),
			     "WARC/1.0\r\n"
			     "WARC-Type: request\r\n"
			     "WARC-Target-URI: http://example.com/\r\n"
			     "Content-Type: application/http; msgtype=response\r\n"
			     "Content-Length: %" INT64 "\r\n"
			     "\r\n"
			     "%s",
			     payloadLen, payload);
	REQUIRE(n > 0);

	ArchiveParser parser;
	parser.init(ARCHIVE_WARC);
	int64_t consumed = 0;
	ParseResult pr = parser.parseNext(buf, n, &consumed);
	REQUIRE(pr == PARSE_SKIP_RECORD);
	REQUIRE(consumed == n);
}

TEST_CASE("ArchiveParser needs more data for partial WARC record", "[ArchiveParser][WARC]") {
	const char *payload =
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: text/html\r\n"
		"Content-Length: 5\r\n"
		"\r\n"
		"Hello";
	int64_t payloadLen = (int64_t)strlen(payload);

	char buf[1024];
	int32_t n = snprintf(buf, sizeof(buf),
			     "WARC/1.0\r\n"
			     "WARC-Type: response\r\n"
			     "WARC-Target-URI: http://example.com/\r\n"
			     "Content-Type: application/http; msgtype=response\r\n"
			     "Content-Length: %" INT64 "\r\n"
			     "\r\n"
			     "%s",
			     payloadLen, payload);
	REQUIRE(n > 0);

	ArchiveParser parser;
	parser.init(ARCHIVE_WARC);
	int64_t consumed = 0;
	ParseResult pr = parser.parseNext(buf, n - 3, &consumed);
	REQUIRE(pr == PARSE_NEED_MORE);
}

TEST_CASE("ArchiveParser needs more data for oversized WARC content-length", "[ArchiveParser][WARC]") {
	const char *payload = "HTTP/1.1 200 OK\r\n\r\nX";
	int64_t payloadLen = (int64_t)strlen(payload);

	char buf[512];
	int32_t n = snprintf(buf, sizeof(buf),
			     "WARC/1.0\r\n"
			     "WARC-Type: response\r\n"
			     "WARC-Target-URI: http://example.com/oversize\r\n"
			     "Content-Type: application/http; msgtype=response\r\n"
			     "Content-Length: %" INT64 "\r\n"
			     "\r\n"
			     "%s",
			     payloadLen + 100, payload);
	REQUIRE(n > 0);

	ArchiveParser parser;
	parser.init(ARCHIVE_WARC);
	int64_t consumed = 0;
	ParseResult pr = parser.parseNext(buf, n, &consumed);
	REQUIRE(pr == PARSE_NEED_MORE);
}

TEST_CASE("ArchiveParser errors on missing WARC Content-Length", "[ArchiveParser][WARC]") {
	const char *payload = "HTTP/1.1 200 OK\r\n\r\nX";

	char buf[512];
	int32_t n = snprintf(buf, sizeof(buf),
			     "WARC/1.0\r\n"
			     "WARC-Type: response\r\n"
			     "WARC-Target-URI: http://example.com/missing-len\r\n"
			     "Content-Type: application/http; msgtype=response\r\n"
			     "\r\n"
			     "%s",
			     payload);
	REQUIRE(n > 0);

	ArchiveParser parser;
	parser.init(ARCHIVE_WARC);
	int64_t consumed = 0;
	ParseResult pr = parser.parseNext(buf, n, &consumed);
	REQUIRE(pr == PARSE_ERROR);
}

TEST_CASE("ArchiveParser skips WARC record missing Content-Type", "[ArchiveParser][WARC]") {
	const char *payload = "HTTP/1.1 200 OK\r\n\r\nX";
	int64_t payloadLen = (int64_t)strlen(payload);

	char buf[512];
	int32_t n = snprintf(buf, sizeof(buf),
			     "WARC/1.0\r\n"
			     "WARC-Type: response\r\n"
			     "WARC-Target-URI: http://example.com/missing-ct\r\n"
			     "Content-Length: %" INT64 "\r\n"
			     "\r\n"
			     "%s",
			     payloadLen, payload);
	REQUIRE(n > 0);

	ArchiveParser parser;
	parser.init(ARCHIVE_WARC);
	int64_t consumed = 0;
	ParseResult pr = parser.parseNext(buf, n, &consumed);
	REQUIRE(pr == PARSE_SKIP_RECORD);
	REQUIRE(consumed == n);
}

TEST_CASE("ArchiveParser skips WARC record with non-response content-type", "[ArchiveParser][WARC]") {
	const char *payload = "HTTP/1.1 200 OK\r\n\r\nX";
	int64_t payloadLen = (int64_t)strlen(payload);

	char buf[512];
	int32_t n = snprintf(buf, sizeof(buf),
			     "WARC/1.0\r\n"
			     "WARC-Type: response\r\n"
			     "WARC-Target-URI: http://example.com/nonresp\r\n"
			     "Content-Type: application/http; msgtype=request\r\n"
			     "Content-Length: %" INT64 "\r\n"
			     "\r\n"
			     "%s",
			     payloadLen, payload);
	REQUIRE(n > 0);

	ArchiveParser parser;
	parser.init(ARCHIVE_WARC);
	int64_t consumed = 0;
	ParseResult pr = parser.parseNext(buf, n, &consumed);
	REQUIRE(pr == PARSE_SKIP_RECORD);
	REQUIRE(consumed == n);
}

TEST_CASE("ArchiveParser parses a basic ARC record", "[ArchiveParser][ARC]") {
	const char *payload =
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: text/html\r\n"
		"Content-Length: 5\r\n"
		"\r\n"
		"Hello";
	int64_t payloadLen = (int64_t)strlen(payload);

	char buf[1024];
	int32_t n = snprintf(buf, sizeof(buf),
			     "\n"
			     "http://example.com/ 1.2.3.4 20240101010203 text/html %" INT64 "\n"
			     "%s",
			     payloadLen, payload);
	REQUIRE(n > 0);

	ArchiveParser parser;
	parser.init(ARCHIVE_ARC);
	int64_t consumed = 0;
	ParseResult pr = parser.parseNext(buf, n, &consumed);
	REQUIRE(pr == PARSE_OK);
	REQUIRE(consumed == n);

	const ArchiveRecord &rec = parser.getRecord();
	REQUIRE(rec.urlLen == (int32_t)strlen("http://example.com/"));
	REQUIRE(strncmp(rec.url, "http://example.com/", rec.urlLen) == 0);
	REQUIRE(rec.payloadLen == payloadLen);
	REQUIRE(rec.contentType == CT_HTML);
}

TEST_CASE("ArchiveParser handles ARC record with LF delimiters", "[ArchiveParser][ARC]") {
	const char *payload = "HTTP/1.0 200 OK\n\nX";
	int64_t payloadLen = (int64_t)strlen(payload);

	char buf[512];
	int32_t n = snprintf(buf, sizeof(buf),
			     "\n"
			     "http://example.com/arc 9.9.9.9 20240101010203 text/plain %" INT64 "\n"
			     "%s",
			     payloadLen, payload);
	REQUIRE(n > 0);

	ArchiveParser parser;
	parser.init(ARCHIVE_ARC);
	int64_t consumed = 0;
	ParseResult pr = parser.parseNext(buf, n, &consumed);
	REQUIRE(pr == PARSE_OK);
	REQUIRE(consumed == n);

	const ArchiveRecord &rec = parser.getRecord();
	REQUIRE(rec.urlLen == (int32_t)strlen("http://example.com/arc"));
	REQUIRE(rec.contentType == CT_TEXT);
}

int main(int argc, char* argv[]) {
	g_conf.m_runAsDaemon = false;
	g_conf.m_logToFile = false;
	char stackPointTestAnchor;
	g_mem.setStackPointer(&stackPointTestAnchor);
	g_mem.m_memtablesize = 2048;

	int result = Catch::Session().run(argc, argv);

	return result;
}
