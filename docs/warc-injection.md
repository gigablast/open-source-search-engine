# WARC/ARC Injection Flow

This document summarizes how WARC/ARC injection works in this codebase,
which components are involved, and what parsing behaviors and limitations
exist today.

## Entry Points

- HTTP injection endpoint: `sendPageInject()` in `src/PageInject.cpp` parses
  request params into an `InjectionRequest` and forwards it via `Msg7`.
- CLI injection command: `inject` in `src/main.cpp` parses input files and
  sends HTTP requests directly to `/inject` or `/admin/inject`.
- UDP injection handler: `handleRequest7()` in `src/PageInject.cpp` receives
  serialized `InjectionRequest` (msg type `0x07`) and calls
  `XmlDoc::injectDoc()` in `src/XmlDoc.cpp`.
- WARC/ARC detection: `Url::isWarc()` / `Url::isArc()` in `src/Url.cpp` detect
  `.warc`, `.warc.gz`, `.arc`, `.arc.gz`. This influences indexing behavior.
- WARC load-balancing: `getHostToHandleInjection()` in `src/PageInject.cpp`
  treats WARC URLs specially to spread archive work across hosts.

## High-Level Flow

1) Client submits `/admin/inject` (or CLI equivalent) with a target URL.
2) `sendPageInject()` builds an `InjectionRequest` via
   `setInjectionRequestFromParms()` and sends it to a host using
   `Msg7::sendInjectionRequestToHost()`.
3) The CLI `inject` path builds HTTP requests and sends them via
   `s_tcp.sendMsg()` without using the UDP injection request format.
3) The receiving host runs `handleRequest7()`, deserializes the request,
   builds an `XmlDoc`, and calls `XmlDoc::injectDoc()`.
4) `XmlDoc::injectDoc()` calls `set4()` to configure the document and then
   `indexDoc()`.
5) `XmlDoc::indexDoc()` checks if the URL is a WARC/ARC container
   (`m_firstUrl.isWarc()` or `m_firstUrl.isArc()`). If so, it calls
   `indexWarcOrArc()` and does not index the container itself.

## WARC/ARC Streaming and Parsing

### CLI `inject` Parsing (Separate Codepath)

The CLI `inject` command has its own parser for WARC/ARC and for the
`+++URL:` format:

- WARC/ARC parsing lives in `doInjectWarc()` / `doInjectArc()` in
  `src/main.cpp`.
- It reads fixed-size blocks from disk, scans for WARC/ARC headers, and then
  sends per-record HTTP requests to `/admin/inject`.
- This code does not reuse `XmlDoc::indexWarcOrArc()` or the streaming
  pipe/`getUtf8ContentInFile()` logic.

### Download / Stream

`XmlDoc::getUtf8ContentInFile()` streams the WARC/ARC content:

- For normal URLs, this code is only used for large files; WARC/ARC always
  uses it.
- It constructs a shell pipeline: `wget ... | zcat | mbuffer ...`
  and opens it with `gbpopen()` for streaming reads.
- The file descriptor is set non-blocking and a read callback is registered.

### Buffering

`XmlDoc::readMoreWarc()` incrementally reads from the pipe into `m_fileBuf`:

- Uses a fixed buffer sized to `(5 * MAXWARCRECSIZE) + 1`.
- Tracks processed bytes via `m_fptr`/`m_fptrEnd`.
- Uses `memmove()` only when necessary to avoid repeated shifting.
- Can skip over oversized records by advancing `m_fptr`.

### Record Parsing

`XmlDoc::indexWarcOrArc()` scans the buffer and extracts record metadata:

WARC:
- Finds `WARC/` within a small window from `m_fptr`.
- Splits headers at `\r\n\r\n`.
- Looks up:
  - `Content-Length` (required)
  - `WARC-Target-URI` (required)
  - `WARC-Type` (must be `response`)
  - `Content-Type` (must be `application/http; msgtype=response`)
  - `WARC-Date` (optional)
  - `WARC-IP-Address` (optional)
- Skips any non-`response` records.

ARC:
- Scans for the next `\nhttp://` or `\nhttps://`.
- Parses a single header line containing:
  - URL
  - IP string
  - timestamp
  - content type
  - content length
- Skips non-indexable content types.

### Subdocument Injection

For each WARC/ARC record, `indexWarcOrArc()`:

- Builds a `Msg7` and `InjectionRequest`.
- Copies the HTTP payload into `msg7->m_contentBuf` to avoid buffer reuse
  issues.
- Injects the record via `Msg7::sendInjectionRequestToHost()`.
- Sets metadata fields, including:
  - `m_firstIndexed`/`m_lastSpidered` from `WARC-Date`
  - `m_injectDocIp` from `WARC-IP-Address`
  - Additional JSON metadata key `gbcapturedate`
- Waits for all sub-injections to complete before finishing.

## Current Parsing Limitations and Improvement Ideas

These are suggestions based on the current parsing logic in
`src/XmlDoc.cpp` and related helpers.

1) Header matching is case-sensitive and string-based.
   - WARC fields should be matched case-insensitively.
   - Parse Content-Type parameters instead of strict string equality.
2) `WARC/` discovery only scans a small window.
   - The loop only checks ~10 bytes for `WARC/`, which can fail if
     records are not aligned at `m_fptr`.
3) `Content-Length` and record bounds are trusted too much.
   - Validate that `recContentLen` is sane and does not exceed buffer
     bounds before using it to advance pointers.
4) No support for WARC 1.1 or non-standard line endings.
   - Accept `\n\n` header terminators and `WARC/1.1`.
5) ARC parsing expects a very strict header layout.
   - Handle extra whitespace and `\r\n` and report structured errors.
6) Content-Type filtering is narrow.
   - Consider allowing `application/http; msgtype=response` with optional
     parameters or mixed casing.
7) Skipping large records can desync parsing.
   - The skip-ahead logic adjusts pointers but does not validate that the
     next record boundary is sane.
8) Error reporting is log-only.
   - Track metrics for skipped records, parse failures, and oversized
     entries to aid debugging.
9) URI normalization and validation is minimal.
   - Trim whitespace around `WARC-Target-URI` and validate UTF-8.
10) Unit tests are missing for parser edge cases.
    - Add fixture-based tests for WARC/ARC parsing, especially for:
      - Mixed-case headers
      - Extra whitespace and line endings
      - Large `Content-Length`
      - Non-response records
      - Missing required fields

11) CLI parsing is duplicated and divergent.
    - Refactor WARC/ARC parsing into a shared helper used by both
      `src/main.cpp` (CLI) and `src/XmlDoc.cpp` (server indexing).
    - The helper should accept a stream or buffer window and yield
      record metadata + payload slices without modifying the underlying
      buffer (so both streaming and file-based readers can share logic).

## Draft Helper API Proposal

Goal: centralize WARC/ARC parsing so both server-side injection
(`XmlDoc::indexWarcOrArc()`) and CLI injection (`doInjectWarc/doInjectArc`)
use the same parsing and validation logic.

### Suggested Types

- `struct ArchiveRecord`:
  - `char *url`, `int32_t urlLen`
  - `char *payload`, `int64_t payloadLen`
  - `int64_t captureTime`
  - `int32_t ip` (0 if unknown)
  - `char contentType` or parsed `int32_t ct`
  - `bool hasHttpResponse`

- `enum ArchiveType { ARCHIVE_WARC, ARCHIVE_ARC }`

- `class ArchiveParser`:
  - `bool init(ArchiveType type)`
  - `ParseResult parseNext(const char *buf, int64_t bufLen, int64_t *consumed)`
  - `ArchiveRecord getRecord() const`
  - `int32_t lastError() const`

### Integration Points

- `XmlDoc::indexWarcOrArc()`:
  - Use `ArchiveParser::parseNext()` on `m_fileBuf` windows.
  - When a record is ready, build `InjectionRequest` and inject.
  - Maintain existing stream buffering but remove ad-hoc header parsing.

- `doInjectWarc()` / `doInjectArc()` in `src/main.cpp`:
  - Replace current scanning logic with `ArchiveParser`.
  - Keep the CLI file-read loop but let parser decide record boundaries.

### Behavior Notes

- Parser should accept:
  - `WARC/1.0` and `WARC/1.1`
  - Header field names case-insensitively
  - `\r\n\r\n` and `\n\n` header terminators
  - Content-Type parameters (e.g., `application/http; msgtype=response; charset=...`)
- Record rejection should return a structured error and the number of bytes
  to skip so callers can continue streaming safely.

## Implementation Status

- [x] Add shared `ArchiveParser` types and first-pass WARC/ARC parsing
- [x] Wire CLI `inject` (`doInjectWarc`/`doInjectArc`) to `ArchiveParser`
- [ ] Migrate server-side `XmlDoc::indexWarcOrArc()` to `ArchiveParser`
- [ ] Add parser-focused tests and fixtures for edge cases

## Key Files

- `src/PageInject.cpp`
- `src/PageInject.h`
- `src/XmlDoc.cpp`
- `src/XmlDoc.h`
- `src/Url.cpp`
- `src/Url.h`
