#include <string.h>
#include "http_response.h"

long HTTPFindHeaderEnd(const char *buf, long len)
{
	long i;
	short lineBreaks = 0;

	for (i = 0; i < len; i++) {
		if (buf[i] == '\r' || buf[i] == '\n') {
			if (buf[i] == '\r' && i + 1 < len && buf[i + 1] == '\n') {
				i++;
			}
			lineBreaks++;
			if (lineBreaks == 2) {
				return i + 1;
			}
		} else {
			lineBreaks = 0;
		}
	}
	return -1;
}

short HTTPParseStatusCode(const char *headerBuf, long headerLen)
{
	const char *p = headerBuf;
	const char *end = headerBuf + headerLen;
	short status = 0;

	while (p < end && *p != ' ') p++;
	while (p < end && *p == ' ') p++;
	while (p < end && *p >= '0' && *p <= '9') {
		status = (short)(status * 10 + (*p - '0'));
		p++;
	}
	return status;
}

static bool HeaderNameEquals(const char *line, const char *name, long len)
{
	long i;

	for (i = 0; i < len; i++) {
		char a = line[i];
		char b = name[i];

		if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
		if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
		if (a != b) return false;
	}
	return name[len] == '\0';
}

long HTTPParseContentLength(const char *headerBuf, long headerLen)
{
	const char *line = headerBuf;
	const char *end = headerBuf + headerLen;

	while (line < end) {
		const char *lineEnd = line;
		const char *colon = NULL;
		long value = 0;

		while (lineEnd < end && *lineEnd != '\r' && *lineEnd != '\n') {
			if (!colon && *lineEnd == ':') colon = lineEnd;
			lineEnd++;
		}

		if (colon && HeaderNameEquals(line, "Content-Length",
				(long)(colon - line))) {
			const char *valuePtr = colon + 1;
			while (valuePtr < lineEnd && (*valuePtr == ' ' || *valuePtr == '\t')) {
				valuePtr++;
			}
			while (valuePtr < lineEnd && *valuePtr >= '0' && *valuePtr <= '9') {
				value = (value * 10) + (*valuePtr - '0');
				valuePtr++;
			}
			return value;
		}

		line = lineEnd;
		while (line < end && (*line == '\r' || *line == '\n')) line++;
	}

	return -1;
}

bool HTTPLooksLikeResponse(const char *buf, long len)
{
	if (len < 5) return false;
	return strncmp(buf, "HTTP/", 5) == 0;
}

unsigned short HTTPClipBodyBytes(long contentLength, long bodyBytesRead,
		unsigned short bytesRead)
{
	long remaining;

	if (contentLength < 0) return bytesRead;

	remaining = contentLength - bodyBytesRead;
	if (remaining <= 0) return 0;
	if (bytesRead > remaining) return (unsigned short)remaining;
	return bytesRead;
}
